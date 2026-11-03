//! Per-fragment operations, one cell each on the top screen (4x2 grid, 100x120 px per cell).
//! Page 1: alpha test, texture environment. Page 2: stencil, glClear with scissor/masks.
//! A switches pages. The expected result is printed on the bottom screen.
#[path = "../common/mod.rs"]
mod common;

use c3dgl::*;
use common::gl::*;
use common::{App, Renderer, StatsOverlay, SCREEN_HEIGHT, SCREEN_WIDTH};
use ctru::prelude::*;

const COLUMNS: i32 = 4;
const ROWS: i32 = 2;
const CELL_W: i32 = SCREEN_WIDTH / COLUMNS;
const CELL_H: i32 = SCREEN_HEIGHT / ROWS;
const SPRITE_SIZE: usize = 32;
const CHECKER_SIZE: usize = 8;

/// Cell (column, row from the top) as viewport; coordinates inside are x -1..1, y -1.2..1.2
/// (square pixels). z = 1 is nearest.
unsafe fn begin_cell(column: i32, row: i32) {
    glViewport(column * CELL_W, (ROWS - 1 - row) * CELL_H, CELL_W, CELL_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.2, 1.2, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

unsafe fn set_texture_params(filter: u32) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter as i32);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter as i32);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE as i32);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE as i32);
}

unsafe fn create_texture(format: u32, size: usize, pixels: &[u8], filter: u32) -> u32 {
    let mut id = 0;
    glGenTextures(1, &mut id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, format as i32, size as i32, size as i32, 0, format, GL_UNSIGNED_BYTE, pixels.as_ptr().cast());
    set_texture_params(filter);
    id
}

fn sprite_alpha(x: usize, y: usize) -> f32 {
    let half = SPRITE_SIZE as f32 / 2.0;
    let (dx, dy) = (x as f32 + 0.5 - half, y as f32 + 0.5 - half);
    (1.0 - (dx * dx + dy * dy).sqrt() / half).max(0.0)
}

/// Orange sprite whose alpha falls off linearly from 1 in the center to 0 at the edge:
/// alpha >= 0.5 is a disc of half the sprite size.
unsafe fn create_sprite() -> u32 {
    let mut pixels = vec![0u8; SPRITE_SIZE * SPRITE_SIZE * 4];
    for y in 0..SPRITE_SIZE {
        for x in 0..SPRITE_SIZE {
            pixels[(y * SPRITE_SIZE + x) * 4..][..4].copy_from_slice(&[255, 140, 0, (sprite_alpha(x, y) * 255.0) as u8]);
        }
    }
    create_texture(GL_RGBA, SPRITE_SIZE, &pixels, GL_LINEAR)
}

/// Alpha-only (GL_ALPHA) version of the sprite: just the soft disc.
unsafe fn create_alpha_disc() -> u32 {
    let mut pixels = vec![0u8; SPRITE_SIZE * SPRITE_SIZE];
    for y in 0..SPRITE_SIZE {
        for x in 0..SPRITE_SIZE {
            pixels[y * SPRITE_SIZE + x] = (sprite_alpha(x, y) * 255.0) as u8;
        }
    }
    create_texture(GL_ALPHA, SPRITE_SIZE, &pixels, GL_LINEAR)
}

/// White/black 2x2 px checker, 4x4 squares, as GL_RGB or GL_LUMINANCE.
unsafe fn create_checker(format: u32) -> u32 {
    let bpp = if format == GL_RGB { 3 } else { 1 };
    let mut pixels = vec![0u8; CHECKER_SIZE * CHECKER_SIZE * bpp];
    for y in 0..CHECKER_SIZE {
        for x in 0..CHECKER_SIZE {
            let v = if (x / 2 + y / 2) % 2 == 0 { 255 } else { 0 };
            pixels[(y * CHECKER_SIZE + x) * bpp..][..bpp].fill(v);
        }
    }
    create_texture(format, CHECKER_SIZE, &pixels, GL_NEAREST)
}

unsafe fn draw_outline(size: f32) {
    glColor3f(0.5, 0.5, 0.5);
    glBegin(GL_LINE_LOOP);
    glVertex2f(-size, -size);
    glVertex2f(size, -size);
    glVertex2f(size, size);
    glVertex2f(-size, size);
    glEnd();
}

/// White quad with vertex alpha 0 (left) -> 1 (right), alpha tested with func/0.5, inside a gray outline.
unsafe fn draw_alpha_gradient(func: u32) {
    draw_outline(0.82);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(func, 0.5);
    glBegin(GL_QUADS);
    glColor4f(1.0, 1.0, 1.0, 0.0);
    glVertex2f(-0.8, -0.8);
    glColor4f(1.0, 1.0, 1.0, 1.0);
    glVertex2f(0.8, -0.8);
    glColor4f(1.0, 1.0, 1.0, 1.0);
    glVertex2f(0.8, 0.8);
    glColor4f(1.0, 1.0, 1.0, 0.0);
    glVertex2f(-0.8, 0.8);
    glEnd();
    glDisable(GL_ALPHA_TEST);
}

/// Textured quad from (x0, y0) to (x1, y1) in the current color.
unsafe fn draw_textured_quad(texture: u32, x0: f32, y0: f32, x1: f32, y1: f32, z: f32) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0, 0.0);
    glVertex3f(x0, y0, z);
    glTexCoord2f(1.0, 0.0);
    glVertex3f(x1, y0, z);
    glTexCoord2f(1.0, 1.0);
    glVertex3f(x1, y1, z);
    glTexCoord2f(0.0, 1.0);
    glVertex3f(x0, y1, z);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

unsafe fn draw_sprite(sprite: u32, size: f32, z: f32) {
    glColor3f(1.0, 1.0, 1.0);
    draw_textured_quad(sprite, -size, -size, size, size, z);
}

/// Cutout sprite (alpha >= 0.5) over blue/white stripes, no blending: a hard-edged orange disc.
unsafe fn draw_cutout(sprite: u32) {
    glBegin(GL_QUADS);
    for i in 0..8 {
        let x = -0.8 + 0.2 * i as f32;
        if i % 2 == 1 {
            glColor3f(1.0, 1.0, 1.0);
        } else {
            glColor3f(0.2, 0.3, 0.9);
        }
        glVertex2f(x, -0.8);
        glVertex2f(x + 0.2, -0.8);
        glVertex2f(x + 0.2, 0.8);
        glVertex2f(x, 0.8);
    }
    glEnd();
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GEQUAL, 0.5);
    draw_sprite(sprite, 0.8, 0.0);
    glDisable(GL_ALPHA_TEST);
}

/// Discarded fragments must not write depth: the cutout sprite is drawn in front FIRST, then a green
/// quad behind it. The green must show all around the disc (no background-colored square).
unsafe fn draw_cutout_depth(sprite: u32) {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GEQUAL, 0.5);
    draw_sprite(sprite, 0.8, 0.5);
    glDisable(GL_ALPHA_TEST);
    glColor3f(0.2, 0.7, 0.3);
    glBegin(GL_QUADS);
    glVertex3f(-0.8, -0.8, -0.5);
    glVertex3f(0.8, -0.8, -0.5);
    glVertex3f(0.8, 0.8, -0.5);
    glVertex3f(-0.8, 0.8, -0.5);
    glEnd();
    glDisable(GL_DEPTH_TEST);
}

/// Texture environment: a quad in the top half and one in the bottom half of the cell.
unsafe fn draw_top_bottom(mode: u32, top: u32, bottom: u32, blend_bottom: bool) {
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, mode as i32);
    draw_textured_quad(top, -0.8, 0.1, 0.8, 1.1, 0.0);
    if blend_bottom {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    draw_textured_quad(bottom, -0.8, -1.1, 0.8, -0.1, 0.0);
    glDisable(GL_BLEND);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE as i32);
}

struct Textures {
    sprite: u32,
    alpha_disc: u32,
    rgb_checker: u32,
    luminance_checker: u32,
}

/// GL_MODULATE, green vertex color: RGB checker -> green/black; GL_ALPHA disc -> soft green disc (not black).
unsafe fn draw_modulate(t: &Textures) {
    glColor3f(0.2, 0.9, 0.2);
    draw_top_bottom(GL_MODULATE, t.rgb_checker, t.alpha_disc, true);
}

/// GL_REPLACE, green vertex color: RGB checker -> white/black (no green); RGBA sprite -> soft orange disc.
unsafe fn draw_replace(t: &Textures) {
    glColor3f(0.2, 0.9, 0.2);
    draw_top_bottom(GL_REPLACE, t.rgb_checker, t.sprite, true);
}

/// GL_DECAL, green vertex color, no blending: opaque green square with the orange disc fading into it.
unsafe fn draw_decal(t: &Textures) {
    glColor3f(0.2, 0.9, 0.2);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL as i32);
    draw_textured_quad(t.sprite, -0.8, -0.8, 0.8, 0.8, 0.0);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE as i32);
}

/// Luminance checker. GL_BLEND, yellow vertex color, blue env color: blue (white squares)/yellow
/// (black squares). GL_ADD, dark red vertex color: white/dark red.
unsafe fn draw_blend_add(t: &Textures) {
    let blue = [0.1f32, 0.3, 1.0, 1.0];
    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, blue.as_ptr());
    glColor3f(1.0, 0.9, 0.1);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND as i32);
    draw_textured_quad(t.luminance_checker, -0.8, 0.1, 0.8, 1.1, 0.0);
    glColor3f(0.5, 0.0, 0.0);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD as i32);
    draw_textured_quad(t.luminance_checker, -0.8, -1.1, 0.8, -0.1, 0.0);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE as i32);
}

//----------------------------------------------------------------------------------
// Stencil and clear tests (page 2). The frame starts with stencil cleared to 3 by the memory fill.
//----------------------------------------------------------------------------------

/// Scissor box of the cell, inset by `inset` px.
unsafe fn scissor_cell(column: i32, row: i32, inset: i32) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(column * CELL_W + inset, (ROWS - 1 - row) * CELL_H + inset, CELL_W - 2 * inset, CELL_H - 2 * inset);
}

/// Clear only the stencil of the cell (quad path: stencil without depth).
unsafe fn clear_cell_stencil(column: i32, row: i32) {
    scissor_cell(column, row, 0);
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}

unsafe fn draw_disc(x: f32, y: f32, r: f32, z: f32) {
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(x, y, z);
    for i in 0..=32 {
        let a = 6.2831853 * i as f32 / 32.0;
        glVertex3f(x + r * a.cos(), y + r * a.sin(), z);
    }
    glEnd();
}

unsafe fn draw_rect(x0: f32, y0: f32, x1: f32, y1: f32, z: f32) {
    glBegin(GL_QUADS);
    glVertex3f(x0, y0, z);
    glVertex3f(x1, y0, z);
    glVertex3f(x1, y1, z);
    glVertex3f(x0, y1, z);
    glEnd();
}

/// Blue/white vertical stripes over the whole cell.
unsafe fn draw_stripes(z: f32) {
    for i in 0..10 {
        if i % 2 == 1 {
            glColor3f(1.0, 1.0, 1.0);
        } else {
            glColor3f(0.2, 0.3, 0.9);
        }
        draw_rect(-1.0 + 0.2 * i as f32, -1.2, -0.8 + 0.2 * i as f32, 1.2, z);
    }
}

/// Stencil = 1 inside a disc, nothing drawn to color.
unsafe fn write_stencil_disc() {
    glEnable(GL_STENCIL_TEST);
    glColorMask(0, 0, 0, 0);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    draw_disc(0.0, 0.0, 0.7, 0.0);
    glColorMask(1, 1, 1, 1);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}

/// EQUAL 1: stripes only inside the disc.
unsafe fn draw_stencil_inside() {
    write_stencil_disc();
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    draw_stripes(0.0);
    glDisable(GL_STENCIL_TEST);
}

/// EQUAL 3 (the value of the frame clear): stripes only outside the disc.
unsafe fn draw_stencil_clear_value() {
    write_stencil_disc();
    glStencilFunc(GL_EQUAL, 3, 0xFF);
    draw_stripes(0.0);
    glDisable(GL_STENCIL_TEST);
}

/// INCR with three overlapping discs, then one color per count: 1 blue, 2 green, 3 yellow.
unsafe fn draw_stencil_count(column: i32, row: i32) {
    clear_cell_stencil(column, row);
    glEnable(GL_STENCIL_TEST);
    glColorMask(0, 0, 0, 0);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    draw_disc(-0.3, 0.25, 0.5, 0.0);
    draw_disc(0.3, 0.25, 0.5, 0.0);
    draw_disc(0.0, -0.25, 0.5, 0.0);
    glColorMask(1, 1, 1, 1);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    let colors = [[0.2f32, 0.3, 0.9], [0.2, 0.8, 0.3], [1.0, 0.9, 0.2]];
    for count in 1..=3 {
        glStencilFunc(GL_EQUAL, count, 0xFF);
        let c = colors[count as usize - 1];
        glColor3f(c[0], c[1], c[2]);
        draw_rect(-1.0, -1.2, 1.0, 1.2, 0.0);
    }
    glDisable(GL_STENCIL_TEST);
}

/// INVERT through write mask 0x0F on two overlapping squares, then EQUAL 0x0F: magenta squares,
/// empty overlap. Without the write mask the value would be 0xFF and nothing would show.
unsafe fn draw_stencil_invert(column: i32, row: i32) {
    clear_cell_stencil(column, row);
    glEnable(GL_STENCIL_TEST);
    glColorMask(0, 0, 0, 0);
    glStencilMask(0x0F);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
    draw_rect(-0.8, -0.2, 0.3, 0.9, 0.0);
    draw_rect(-0.3, -0.9, 0.8, 0.2, 0.0);
    glStencilMask(0xFF);
    glColorMask(1, 1, 1, 1);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glStencilFunc(GL_EQUAL, 0x0F, 0xFF);
    glColor3f(0.9, 0.2, 0.9);
    draw_rect(-1.0, -1.2, 1.0, 1.2, 0.0);
    glDisable(GL_STENCIL_TEST);
}

/// glClear with a scissor box 20 px inside the cell: red box with a background-colored border.
unsafe fn draw_scissor_clear(column: i32, row: i32) {
    scissor_cell(column, row, 20);
    glClearColor(0.9, 0.1, 0.1, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(0.12, 0.12, 0.15, 1.0);
    glDisable(GL_SCISSOR_TEST);
}

/// Red left/blue right half, then a green-only clear to white 20 px inside the cell:
/// red/blue frame around yellow (left) and cyan (right).
unsafe fn draw_color_mask_clear(column: i32, row: i32) {
    glColor3f(1.0, 0.0, 0.0);
    draw_rect(-1.0, -1.2, 0.0, 1.2, 0.0);
    glColor3f(0.0, 0.0, 1.0);
    draw_rect(0.0, -1.2, 1.0, 1.2, 0.0);
    scissor_cell(column, row, 20);
    glColorMask(0, 1, 0, 0);
    glClearColor(1.0, 1.0, 1.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(0.12, 0.12, 0.15, 1.0);
    glColorMask(1, 1, 1, 1);
    glDisable(GL_SCISSOR_TEST);
}

/// Depth-only clear keeps stencil: stencil disc, green quad in front, depth cleared, stripes behind
/// with EQUAL 1 -> stripes in the disc over green. Uncleared depth or lost stencil: only green.
unsafe fn draw_depth_clear_keeps_stencil(column: i32, row: i32) {
    write_stencil_disc();
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glColor3f(0.2, 0.7, 0.3);
    draw_rect(-1.0, -1.2, 1.0, 1.2, 0.5);
    scissor_cell(column, row, 0);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    draw_stripes(-0.5);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);
}

/// Stencil-only clear keeps depth: small green square in front, stencil cleared to 0, red quad
/// behind with EQUAL 0 -> red around the green square. Lost depth: all red; uncleared stencil: only green.
unsafe fn draw_stencil_clear_keeps_depth(column: i32, row: i32) {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glColor3f(0.2, 0.7, 0.3);
    draw_rect(-0.4, -0.4, 0.4, 0.4, 0.5);
    clear_cell_stencil(column, row);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glColor3f(0.9, 0.1, 0.1);
    draw_rect(-1.0, -1.2, 1.0, 1.2, -0.5);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);
}

fn print_page(page: usize) {
    print!("\x1b[2J\x1b[1;1Hc3dgl fragment test, page {}/2\n\n\n\n\n", page + 1);
    if page == 0 {
        print!(
            "Expected on the top screen,\nleft to right, top row (alpha test):\n- GREATER 0.5: white RIGHT half\n  of the gray outline\n- LESS 0.5: white LEFT half\n- cutout: hard orange disc on\n  blue/white stripes\n- cutout + depth: orange disc,\n  green all around it\nbottom row (texture env):\n- MODULATE: green/black checker;\n  soft green disc (alpha tex)\n- REPLACE: white/black checker;\n  soft orange disc\n- DECAL: orange disc fading\n  into an opaque green square\n- BLEND: blue/yellow checker;\n  ADD: white/dark red checker\n"
        );
    } else {
        print!(
            "Expected on the top screen,\nleft to right, top row (stencil):\n- EQUAL: stripes INSIDE a disc\n- clear value: stripes OUTSIDE\n  the disc\n- INCR: 3 discs, blue/green/yellow\n  for 1/2/3 overlaps\n- INVERT + write mask: 2 magenta\n  squares, overlap empty\nbottom row (glClear):\n- scissor: red box, dark border\n- color mask: yellow | cyan in a\n  red | blue frame\n- depth only: stripes in a disc\n  on green\n- stencil only: green square in\n  a red cell\n"
        );
    }
    print!("\nA: next page   START: exit\n");
}

fn main() {
    let gfx = Gfx::new().expect("gfx");
    let _console = Console::new(gfx.bottom_screen.borrow_mut());
    let mut app = App::new();
    let Some(_renderer) = Renderer::new(&mut app, &gfx) else { return };

    let textures = unsafe {
        let textures = Textures {
            sprite: create_sprite(),
            alpha_disc: create_alpha_disc(),
            rgb_checker: create_checker(GL_RGB),
            luminance_checker: create_checker(GL_LUMINANCE),
        };
        glClearColor(0.12, 0.12, 0.15, 1.0);
        textures
    };

    let mut page = 0;
    print_page(page);
    let mut stats = StatsOverlay::new();
    while app.frame() {
        stats.print();
        if app.down(KeyPad::START) {
            break;
        }
        if app.down(KeyPad::A) {
            page = 1 - page;
            print_page(page);
        }
        unsafe {
            if page == 0 {
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                begin_cell(0, 0); draw_alpha_gradient(GL_GREATER);
                begin_cell(1, 0); draw_alpha_gradient(GL_LESS);
                begin_cell(2, 0); draw_cutout(textures.sprite);
                begin_cell(3, 0); draw_cutout_depth(textures.sprite);
                begin_cell(0, 1); draw_modulate(&textures);
                begin_cell(1, 1); draw_replace(&textures);
                begin_cell(2, 1); draw_decal(&textures);
                begin_cell(3, 1); draw_blend_add(&textures);
            } else {
                // Memory fill path: no scissor, full masks, depth + stencil together
                glClearStencil(3);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
                begin_cell(0, 0); draw_stencil_inside();
                begin_cell(1, 0); draw_stencil_clear_value();
                begin_cell(2, 0); draw_stencil_count(2, 0);
                begin_cell(3, 0); draw_stencil_invert(3, 0);
                begin_cell(0, 1); draw_scissor_clear(0, 1);
                begin_cell(1, 1); draw_color_mask_clear(1, 1);
                begin_cell(2, 1); draw_depth_clear_keeps_stencil(2, 1);
                begin_cell(3, 1); draw_stencil_clear_keeps_depth(3, 1);
            }
            c3dglSwapBuffers();
        }
    }
    let ids = [textures.sprite, textures.alpha_disc, textures.rgb_checker, textures.luminance_checker];
    unsafe { glDeleteTextures(4, ids.as_ptr()) };
}

//! Primitives and rasterization, one cell each on the top screen (4x2 grid, 100x120 px per cell).
//! Page 1: every primitive mode. Face culling (GL_BACK, CCW front) is on for all filled shapes, so
//! a strip, fan or quad strip that is assembled with the wrong winding shows up as missing triangles.
//! Page 2: flat shading, polygon modes, edge flags, polygon offset, depth range.
//! Page 3: evaluators (glMap1/2, glEvalMesh, glEvalCoord). A switches pages.
//! The expected result is printed on the bottom screen.
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
const PI: f32 = 3.14159265;

/// Cell (column, row from the top) as viewport; coordinates inside are x -1..1, y -1.2..1.2 (square pixels).
unsafe fn begin_cell(column: i32, row: i32) {
    glViewport(column * CELL_W, (ROWS - 1 - row) * CELL_H, CELL_W, CELL_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.2, 1.2, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

/// GL_POINTS: 3x3 grid, size 1..9 px from the top left.
unsafe fn draw_points() {
    glColor3f(1.0, 1.0, 1.0);
    for i in 0..9 {
        glPointSize((i + 1) as f32);
        glBegin(GL_POINTS);
        glVertex2f(-0.6 + 0.6 * (i % 3) as f32, 0.6 - 0.6 * (i / 3) as f32);
        glEnd();
    }
    glPointSize(1.0);
}

/// GL_LINE_STRIP: open zigzag, 3 px wide, color fading red -> yellow.
unsafe fn draw_line_strip() {
    glLineWidth(3.0);
    glBegin(GL_LINE_STRIP);
    for i in 0..6 {
        glColor3f(1.0, i as f32 / 5.0, 0.0);
        glVertex2f(-0.8 + 0.32 * i as f32, if i % 2 == 1 { 0.6 } else { -0.6 });
    }
    glEnd();
    glLineWidth(1.0);
}

/// GL_LINE_LOOP: closed five-pointed star (the last edge back to the start must be there).
unsafe fn draw_line_loop() {
    glLineWidth(2.0);
    glColor3f(0.3, 1.0, 0.3);
    glBegin(GL_LINE_LOOP);
    for i in 0..5 {
        let a = PI / 2.0 + i as f32 * 4.0 * PI / 5.0;
        glVertex2f(0.8 * a.cos(), 0.8 * a.sin());
    }
    glEnd();
    glLineWidth(1.0);
}

/// GL_TRIANGLE_STRIP: horizontal ribbon of 8 triangles, blue top edge, pink bottom edge. Pairs go
/// top, bottom while moving right, so the first triangle is counter-clockwise.
unsafe fn draw_triangle_strip() {
    glBegin(GL_TRIANGLE_STRIP);
    for i in 0..=4 {
        let x = -0.8 + 0.4 * i as f32;
        glColor3f(0.2, 0.4, 1.0);
        glVertex2f(x, 0.4);
        glColor3f(1.0, 0.4, 0.8);
        glVertex2f(x, -0.4);
    }
    glEnd();
}

unsafe fn rainbow(i: i32, count: i32) {
    let a = 2.0 * PI * i as f32 / count as f32;
    glColor3f(0.5 + 0.5 * a.cos(), 0.5 + 0.5 * (a - 2.1).cos(), 0.5 + 0.5 * (a + 2.1).cos());
}

/// GL_TRIANGLE_FAN: disc with a white center and a rainbow rim.
unsafe fn draw_triangle_fan() {
    const SEGMENTS: i32 = 24;
    glBegin(GL_TRIANGLE_FAN);
    glColor3f(1.0, 1.0, 1.0);
    glVertex2f(0.0, 0.0);
    for i in 0..=SEGMENTS {
        let a = 2.0 * PI * i as f32 / SEGMENTS as f32;
        rainbow(i, SEGMENTS);
        glVertex2f(0.8 * a.cos(), 0.8 * a.sin());
    }
    glEnd();
}

/// GL_QUAD_STRIP: vertical ladder of 4 bands, alternating orange/teal. Pairs go right, left while
/// moving down, so every quad (v[2i], v[2i+1], v[2i+3], v[2i+2]) is counter-clockwise.
unsafe fn draw_quad_strip() {
    glBegin(GL_QUAD_STRIP);
    for i in 0..=4 {
        let y = 0.8 - 0.4 * i as f32;
        if i % 2 == 1 {
            glColor3f(0.1, 0.8, 0.7);
        } else {
            glColor3f(1.0, 0.6, 0.1);
        }
        glVertex2f(0.6, y);
        glVertex2f(-0.6, y);
    }
    glEnd();
}

/// GL_POLYGON from a client array: yellow hexagon.
unsafe fn draw_polygon() {
    let mut vertices = [[0f32; 2]; 6];
    for (i, v) in vertices.iter_mut().enumerate() {
        let a = 2.0 * PI * i as f32 / 6.0;
        *v = [0.8 * a.cos(), 0.8 * a.sin()];
    }
    glColor3f(1.0, 0.9, 0.2);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, vertices.as_ptr().cast());
    glDrawArrays(GL_POLYGON, 0, 6);
    glDisableClientState(GL_VERTEX_ARRAY);
}

/// glDrawElements: red square as GL_TRIANGLE_FAN, white GL_LINE_LOOP outline around it from the same array.
unsafe fn draw_elements() {
    static VERTICES: [f32; 16] = [
        -0.5, -0.5, 0.5, -0.5, 0.5, 0.5, -0.5, 0.5, // Square
        -0.7, -0.7, 0.7, -0.7, 0.7, 0.7, -0.7, 0.7, // Outline
    ];
    static SQUARE: [u16; 4] = [0, 1, 2, 3];
    static OUTLINE: [u8; 4] = [4, 5, 6, 7];
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, VERTICES.as_ptr().cast());
    glColor3f(0.9, 0.2, 0.2);
    glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_SHORT, SQUARE.as_ptr().cast());
    glColor3f(1.0, 1.0, 1.0);
    glLineWidth(2.0);
    glDrawElements(GL_LINE_LOOP, 4, GL_UNSIGNED_BYTE, OUTLINE.as_ptr().cast());
    glLineWidth(1.0);
    glDisableClientState(GL_VERTEX_ARRAY);
}

// Page 2 ------------------------------------------------------------------------------------------

/// GL_FLAT fan: 8 wedges, each one solid in the color of its LAST vertex (no gradients).
unsafe fn draw_flat_fan() {
    glShadeModel(GL_FLAT);
    glBegin(GL_TRIANGLE_FAN);
    glColor3f(1.0, 1.0, 1.0);
    glVertex2f(0.0, 0.0);
    for i in 0..=8 {
        rainbow(i, 8);
        glVertex2f(0.8 * (2.0 * PI * i as f32 / 8.0).cos(), 0.8 * (2.0 * PI * i as f32 / 8.0).sin());
    }
    glEnd();
    glShadeModel(GL_SMOOTH);
}

/// GL_FLAT polygon: hexagon with a different color per vertex, solid RED (the FIRST vertex) above;
/// the same with GL_SMOOTH below for comparison.
unsafe fn draw_flat_polygon() {
    for flat in [true, false] {
        glShadeModel(if flat { GL_FLAT } else { GL_SMOOTH });
        let cy = if flat { 0.55 } else { -0.55 };
        glBegin(GL_POLYGON);
        for i in 0..6 {
            if i == 0 {
                glColor3f(1.0, 0.0, 0.0);
            } else {
                rainbow(i, 6);
            }
            let a = 2.0 * PI * i as f32 / 6.0;
            glVertex2f(0.5 * a.cos(), cy + 0.5 * a.sin());
        }
        glEnd();
    }
    glShadeModel(GL_SMOOTH);
}

/// glPolygonMode GL_LINE: a quad (4 edges, no diagonal) above, a hexagon outline below.
unsafe fn draw_line_mode() {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glLineWidth(2.0);
    glColor3f(0.3, 1.0, 0.3);
    glRectf(-0.6, 0.2, 0.6, 1.0);
    glColor3f(1.0, 0.9, 0.2);
    glBegin(GL_POLYGON);
    for i in 0..6 {
        let a = 2.0 * PI * i as f32 / 6.0;
        glVertex2f(0.5 * a.cos(), -0.55 + 0.5 * a.sin());
    }
    glEnd();
    glLineWidth(1.0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

/// glPolygonMode GL_POINT: the 4 corners of a quad strip of 2 quads -> 6 points (2 rows of 3), 6 px.
unsafe fn draw_point_mode() {
    glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
    glPointSize(6.0);
    glColor3f(1.0, 1.0, 1.0);
    glBegin(GL_QUAD_STRIP);
    for i in 0..3 {
        let x = -0.6 + 0.6 * i as f32;
        glVertex2f(x, 0.5);
        glVertex2f(x, -0.5);
    }
    glEnd();
    glPointSize(1.0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

/// Front GL_FILL, back GL_LINE: counter-clockwise (front) triangle filled above, clockwise (back) outlined below.
unsafe fn draw_front_back() {
    glPolygonMode(GL_FRONT, GL_FILL);
    glPolygonMode(GL_BACK, GL_LINE);
    glLineWidth(2.0);
    glColor3f(0.2, 0.6, 1.0);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.6, 0.2); // CCW
    glVertex2f(0.6, 0.2);
    glVertex2f(0.0, 1.0);
    glVertex2f(-0.6, -1.0); // CW
    glVertex2f(0.0, -0.2);
    glVertex2f(0.6, -1.0);
    glEnd();
    glLineWidth(1.0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

/// Edge flags: a square from two GL_TRIANGLES in GL_LINE mode, the shared diagonal flagged off -> plain square.
unsafe fn draw_edge_flags() {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glLineWidth(2.0);
    glColor3f(1.0, 0.5, 0.1);
    glBegin(GL_TRIANGLES);
    glEdgeFlag(1);
    glVertex2f(-0.6, -0.6);
    glEdgeFlag(1);
    glVertex2f(0.6, -0.6);
    glEdgeFlag(0);
    glVertex2f(0.6, 0.6); // The flag belongs to the edge starting here: the diagonal
    glEdgeFlag(0);
    glVertex2f(-0.6, -0.6); // Diagonal again
    glEdgeFlag(1);
    glVertex2f(0.6, 0.6);
    glEdgeFlag(1);
    glVertex2f(-0.6, 0.6);
    glEnd();
    glEdgeFlag(1);
    glLineWidth(1.0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

/// Polygon offset: a green plane tilted away from the viewer, red decals on it at the SAME depth
/// with GL_LESS. Left decal: glPolygonOffset(-1, -1) pulls it in front, it must be solid red. Right
/// decal: (+1, +1) pushes it behind, it must not show at all. (Without any offset the result is
/// undefined: coplanar but different triangles z-fight.)
unsafe fn draw_polygon_offset() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.1, 0.1, -0.12, 0.12, 0.1, 10.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0, 0.0, -2.5);
    glRotatef(-65.0, 1.0, 0.0, 0.0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glColor3f(0.2, 0.7, 0.3);
    glRectf(-1.0, -1.5, 1.0, 1.5);
    glColor3f(0.9, 0.1, 0.1);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0, -1.0);
    glRectf(-0.8, -1.2, -0.1, 1.2);
    glPolygonOffset(1.0, 1.0);
    glRectf(0.1, -1.2, 0.8, 1.2);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_DEPTH_TEST);
}

/// glDepthRange: blue square drawn first at z = 0.5 (near) with range [0.5, 1], red square drawn
/// second at z = -0.5 (far) with range [0, 0.5] -> the red one ends up in FRONT where they overlap.
unsafe fn draw_depth_range() {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthRange(0.5, 1.0);
    glColor3f(0.2, 0.4, 1.0);
    glBegin(GL_QUADS);
    glVertex3f(-0.8, -0.2, 0.5);
    glVertex3f(0.3, -0.2, 0.5);
    glVertex3f(0.3, 0.9, 0.5);
    glVertex3f(-0.8, 0.9, 0.5);
    glEnd();
    glDepthRange(0.0, 0.5);
    glColor3f(0.9, 0.1, 0.1);
    glBegin(GL_QUADS);
    glVertex3f(-0.3, -0.9, -0.5);
    glVertex3f(0.8, -0.9, -0.5);
    glVertex3f(0.8, 0.2, -0.5);
    glVertex3f(-0.3, 0.2, -0.5);
    glEnd();
    glDepthRange(0.0, 1.0);
    glDisable(GL_DEPTH_TEST);
}

// Page 3 ------------------------------------------------------------------------------------------

/// Cubic Bezier through 4 control points, drawn with glEvalMesh1 as a line; gray control polygon.
static CURVE_POINTS: [[f32; 3]; 4] = [[-0.8, -0.8, 0.0], [-0.6, 0.9, 0.0], [0.6, -0.9, 0.0], [0.8, 0.8, 0.0]];

unsafe fn draw_control_polygon() {
    glColor3f(0.5, 0.5, 0.5);
    glBegin(GL_LINE_STRIP);
    for p in &CURVE_POINTS {
        glVertex3fv(p.as_ptr());
    }
    glEnd();
}

unsafe fn draw_eval_curve() {
    draw_control_polygon();
    glMap1f(GL_MAP1_VERTEX_3, 0.0, 1.0, 3, 4, CURVE_POINTS[0].as_ptr());
    glEnable(GL_MAP1_VERTEX_3);
    glMapGrid1f(30, 0.0, 1.0);
    glColor3f(1.0, 0.9, 0.2);
    glLineWidth(2.0);
    glEvalMesh1(GL_LINE, 0, 30);
    glLineWidth(1.0);
    glDisable(GL_MAP1_VERTEX_3);
}

/// Same curve, colors from a MAP1_COLOR_4 (red -> blue), points sent with glEvalCoord1f over a different domain.
unsafe fn draw_eval_color_curve() {
    draw_control_polygon();
    static COLORS: [[f32; 4]; 2] = [[1.0, 0.2, 0.2, 1.0], [0.3, 0.5, 1.0, 1.0]];
    glMap1f(GL_MAP1_VERTEX_3, 2.0, 4.0, 3, 4, CURVE_POINTS[0].as_ptr());
    glMap1f(GL_MAP1_COLOR_4, 2.0, 4.0, 4, 2, COLORS[0].as_ptr());
    glEnable(GL_MAP1_VERTEX_3);
    glEnable(GL_MAP1_COLOR_4);
    glLineWidth(3.0);
    glBegin(GL_LINE_STRIP);
    for i in 0..=30 {
        glEvalCoord1f(2.0 + 2.0 * i as f32 / 30.0);
    }
    glEnd();
    glLineWidth(1.0);
    glDisable(GL_MAP1_COLOR_4);
    glDisable(GL_MAP1_VERTEX_3);
}

/// glEvalMesh1 GL_POINT: 11 points along the curve.
unsafe fn draw_eval_points() {
    draw_control_polygon();
    glMap1f(GL_MAP1_VERTEX_3, 0.0, 1.0, 3, 4, CURVE_POINTS[0].as_ptr());
    glEnable(GL_MAP1_VERTEX_3);
    glMapGrid1f(10, 0.0, 1.0);
    glColor3f(1.0, 1.0, 1.0);
    glPointSize(5.0);
    glEvalMesh1(GL_POINT, 0, 10);
    glPointSize(1.0);
    glDisable(GL_MAP1_VERTEX_3);
}

/// Rational quadratic (MAP1_VERTEX_4) = exact quarter circle of radius 0.8; gray reference points on the circle.
unsafe fn draw_eval_rational() {
    glColor3f(0.5, 0.5, 0.5);
    glPointSize(3.0);
    glBegin(GL_POINTS);
    for i in 0..=12 {
        let a = 3.14159265 / 2.0 * i as f32 / 12.0;
        glVertex2f(-0.4 + 0.8 * a.cos(), -0.4 + 0.8 * a.sin());
    }
    glEnd();
    glPointSize(1.0);
    let w = 0.70710678f32;
    let points = [[0.4f32, -0.4, 0.0, 1.0], [0.4 * w, 0.4 * w, 0.0, w], [-0.4, 0.4, 0.0, 1.0]];
    glMap1f(GL_MAP1_VERTEX_4, 0.0, 1.0, 4, 3, points[0].as_ptr());
    glEnable(GL_MAP1_VERTEX_4);
    glMapGrid1f(24, 0.0, 1.0);
    glColor3f(0.3, 1.0, 0.3);
    glEvalMesh1(GL_LINE, 0, 24);
    glDisable(GL_MAP1_VERTEX_4);
}

/// Bicubic patch with a bump; corner colors from a bilinear MAP2_COLOR_4, texcoords from MAP2_TEXTURE_COORD_2.
unsafe fn setup_patch(patch: &mut [[[f32; 3]; 4]; 4]) {
    for u in 0..4 {
        for v in 0..4 {
            patch[u][v] = [-0.8 + 0.533 * u as f32, -0.8 + 0.533 * v as f32, if (u == 1 || u == 2) && (v == 1 || v == 2) { 0.8 } else { 0.0 }];
        }
    }
    static COLORS: [[[f32; 4]; 2]; 2] = [[[1.0, 0.2, 0.2, 1.0], [0.2, 1.0, 0.2, 1.0]], [[0.2, 0.4, 1.0, 1.0], [1.0, 1.0, 0.2, 1.0]]];
    static TEXCOORDS: [[[f32; 2]; 2]; 2] = [[[0.0, 0.0], [0.0, 1.0]], [[1.0, 0.0], [1.0, 1.0]]];
    glMap2f(GL_MAP2_VERTEX_3, 0.0, 1.0, 12, 4, 0.0, 1.0, 3, 4, patch[0][0].as_ptr());
    glMap2f(GL_MAP2_COLOR_4, 0.0, 1.0, 8, 2, 0.0, 1.0, 4, 2, COLORS[0][0].as_ptr());
    glMap2f(GL_MAP2_TEXTURE_COORD_2, 0.0, 1.0, 4, 2, 0.0, 1.0, 2, 2, TEXCOORDS[0][0].as_ptr());
    glMapGrid2f(12, 0.0, 1.0, 12, 0.0, 1.0);
}

/// Patch seen from above at an angle.
unsafe fn begin_patch_cell(column: i32, row: i32) {
    begin_cell(column, row);
    glRotatef(-50.0, 1.0, 0.0, 0.0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MAP2_VERTEX_3);
}

unsafe fn end_patch_cell() {
    glDisable(GL_MAP2_VERTEX_3);
    glDisable(GL_MAP2_COLOR_4);
    glDisable(GL_MAP2_TEXTURE_COORD_2);
    glDisable(GL_DEPTH_TEST);
}

unsafe fn draw_eval_surfaces(checker: u32) {
    let mut patch = [[[0f32; 3]; 4]; 4];
    setup_patch(&mut patch);
    begin_patch_cell(0, 1); // Filled, colored corners
    glEnable(GL_MAP2_COLOR_4);
    glEvalMesh2(GL_FILL, 0, 12, 0, 12);
    end_patch_cell();
    begin_patch_cell(1, 1); // Wireframe
    glColor3f(0.5, 0.8, 1.0);
    glEvalMesh2(GL_LINE, 0, 12, 0, 12);
    end_patch_cell();
    begin_patch_cell(2, 1); // Textured
    glEnable(GL_MAP2_TEXTURE_COORD_2);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, checker);
    glColor3f(1.0, 1.0, 1.0);
    glEvalMesh2(GL_FILL, 0, 12, 0, 12);
    glDisable(GL_TEXTURE_2D);
    end_patch_cell();
    begin_patch_cell(3, 1); // Points of a coarser grid, with glEvalPoint2 for the corners in red
    glMapGrid2f(6, 0.0, 1.0, 6, 0.0, 1.0);
    glColor3f(1.0, 1.0, 1.0);
    glPointSize(3.0);
    glEvalMesh2(GL_POINT, 0, 6, 0, 6);
    glColor3f(1.0, 0.2, 0.2);
    glPointSize(6.0);
    glBegin(GL_POINTS);
    glEvalPoint2(0, 0);
    glEvalPoint2(6, 0);
    glEvalPoint2(0, 6);
    glEvalPoint2(6, 6);
    glEnd();
    glPointSize(1.0);
    end_patch_cell();
}

unsafe fn create_checker() -> u32 {
    let mut pixels = [0u8; 8 * 8 * 3];
    for y in 0..8 {
        for x in 0..8 {
            let light = (x / 2 + y / 2) % 2 == 0;
            pixels[(y * 8 + x) * 3..][..3].copy_from_slice(&if light { [255, 220, 120] } else { [40, 60, 160] });
        }
    }
    let mut id = 0;
    glGenTextures(1, &mut id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB as i32, 8, 8, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.as_ptr().cast());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST as i32);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST as i32);
    id
}

fn print_page(page: usize) {
    print!("\x1b[2J\x1b[1;1Hc3dgl primitives test, page {}/3\n\n\n\n\n", page + 1);
    match page {
        0 => print!(
            "Expected on the top screen,\nleft to right, top row:\n- POINTS: 3x3 squares, 1..9 px\n- LINE_STRIP: open zigzag,\n  red -> yellow\n- LINE_LOOP: closed green star\n- TRIANGLE_STRIP: ribbon, blue\n  top to pink bottom, no gaps\nbottom row:\n- TRIANGLE_FAN: full rainbow disc\n- QUAD_STRIP: 4 bands, orange/teal\n- POLYGON: yellow hexagon\n- DrawElements: red square in a\n  closed white outline\n\nAny hole in a filled shape means\nwrong winding (culling is on).\n"
        ),
        2 => print!(
            "Evaluators. Expected,\nleft to right, top row:\n- yellow Bezier curve, gray\n  control polygon\n- same curve, red -> blue\n- 11 white points on the curve\n- rational quarter circle (green)\n  through the gray points\nbottom row (patch with a bump):\n- filled, red/green/blue/yellow\n  corners\n- light blue wireframe\n- checker textured\n- 7x7 points, red corners\n"
        ),
        _ => print!(
            "Expected on the top screen,\nleft to right, top row:\n- FLAT fan: 8 solid wedges\n- FLAT polygon: solid red hexagon\n  above a smooth one\n- LINE mode: square outline (no\n  diagonal), hexagon outline\n- POINT mode: 2 rows of 3 points\nbottom row:\n- front FILL/back LINE: filled\n  triangle, outlined one below\n- edge flags: square outline,\n  no diagonal\n- polygon offset: solid red decal\n  LEFT on a tilted green plane,\n  none on the right (pushed back)\n- depth range: red square in\n  FRONT of the blue one\n"
        ),
    }
    print!("\nA: next page   START: exit\n");
}

fn main() {
    let gfx = Gfx::new().expect("gfx");
    let _console = Console::new(gfx.bottom_screen.borrow_mut());
    let mut app = App::new();
    let Some(_renderer) = Renderer::new(&mut app, &gfx) else { return };

    let checker = unsafe {
        glClearColor(0.12, 0.12, 0.15, 1.0);
        create_checker()
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
            page = (page + 1) % 3;
            print_page(page);
        }
        unsafe {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            match page {
                0 => {
                    glEnable(GL_CULL_FACE);
                    glCullFace(GL_BACK);
                    glFrontFace(GL_CCW);
                    begin_cell(0, 0); draw_points();
                    begin_cell(1, 0); draw_line_strip();
                    begin_cell(2, 0); draw_line_loop();
                    begin_cell(3, 0); draw_triangle_strip();
                    begin_cell(0, 1); draw_triangle_fan();
                    begin_cell(1, 1); draw_quad_strip();
                    begin_cell(2, 1); draw_polygon();
                    begin_cell(3, 1); draw_elements();
                    glDisable(GL_CULL_FACE);
                }
                2 => {
                    begin_cell(0, 0); draw_eval_curve();
                    begin_cell(1, 0); draw_eval_color_curve();
                    begin_cell(2, 0); draw_eval_points();
                    begin_cell(3, 0); draw_eval_rational();
                    draw_eval_surfaces(checker);
                }
                _ => {
                    begin_cell(0, 0); draw_flat_fan();
                    begin_cell(1, 0); draw_flat_polygon();
                    begin_cell(2, 0); draw_line_mode();
                    begin_cell(3, 0); draw_point_mode();
                    begin_cell(0, 1); draw_front_back();
                    begin_cell(1, 1); draw_edge_flags();
                    begin_cell(2, 1); draw_polygon_offset();
                    begin_cell(3, 1); draw_depth_range();
                }
            }
            c3dglSwapBuffers();
        }
    }
    unsafe { glDeleteTextures(1, &checker) };
}

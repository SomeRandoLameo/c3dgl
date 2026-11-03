// c3dgl C++ example: a spinning, textured and vertex-colored cube.
//
// The renderer exposes a C ABI (extern "C" in the header), so it is used from C++
// directly; RAII wrappers below own the libctru/renderer lifetimes. A toggles
// nearest/linear filtering, START exits.
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl_native.h>

#include <array>
#include <cstdint>
#include <cstdio>

namespace {

struct Vertex {
    float position[3];
    float uv[2];
    std::uint8_t color[4];
};

// One face: a normal and two tangent axes, counter-clockwise seen from outside.
struct Face {
    float n[3], u[3], v[3];
    std::uint8_t rgb[3];
};

constexpr std::array<Face, 6> faces{{
    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, {255, 255, 255}},
    {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, {255, 160, 160}},
    {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}, {160, 255, 160}},
    {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, {160, 160, 255}},
    {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}, {255, 255, 160}},
    {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, {160, 255, 255}},
}};

struct Cube {
    std::array<Vertex, 24> vertices{};
    std::array<std::uint16_t, 36> indices{};

    Cube() {
        constexpr float corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        constexpr float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (std::size_t f = 0; f < faces.size(); ++f) {
            const Face &face = faces[f];
            for (std::size_t c = 0; c < 4; ++c) {
                Vertex &out = vertices[f * 4 + c];
                for (int axis = 0; axis < 3; ++axis)
                    out.position[axis] = (face.n[axis] + corner[c][0] * face.u[axis] + corner[c][1] * face.v[axis]) * .5f;
                out.uv[0] = uv[c][0];
                out.uv[1] = uv[c][1];
                out.color[0] = face.rgb[0];
                out.color[1] = face.rgb[1];
                out.color[2] = face.rgb[2];
                out.color[3] = 255;
            }
            constexpr std::uint16_t quad[6] = {0, 1, 2, 0, 2, 3};
            for (std::size_t i = 0; i < 6; ++i) indices[f * 6 + i] = static_cast<std::uint16_t>(f * 4 + quad[i]);
        }
    }
};

// gfxInitDefault()/gfxExit() are owned by the application, not the renderer.
class Gfx {
public:
    Gfx() { gfxInitDefault(); consoleInit(GFX_BOTTOM, nullptr); }
    ~Gfx() { gfxExit(); }
    Gfx(const Gfx &) = delete;
    Gfx &operator=(const Gfx &) = delete;
};

class Renderer {
public:
    Renderer() : ok_(c3dglInit()) {}
    ~Renderer() { if (ok_) c3dglClose(); }
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    explicit operator bool() const { return ok_; }

private:
    bool ok_;
};

class Texture {
public:
    Texture() {
        std::array<std::uint8_t, 16 * 16 * 4> texels{};
        for (unsigned y = 0; y < 16; ++y)
            for (unsigned x = 0; x < 16; ++x) {
                std::uint8_t *p = &texels[(y * 16 + x) * 4];
                const std::uint8_t v = ((x / 4) ^ (y / 4)) & 1 ? 255 : 90;
                p[0] = p[1] = p[2] = v;
                p[3] = 255;
            }
        glGenTextures(1, &id_);
        glBindTexture(GL_TEXTURE_2D, id_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels.data());
        setLinear(false);
        glEnable(GL_TEXTURE_2D);
    }
    ~Texture() { glDeleteTextures(1, &id_); }
    Texture(const Texture &) = delete;
    Texture &operator=(const Texture &) = delete;

    void setLinear(bool linear) {
        const GLint filter = linear ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    }

private:
    GLuint id_ = 0;
};

}  // namespace

int main() {
    Gfx gfx;
    Renderer renderer;
    if (!renderer) {
        std::printf("c3dglInit failed\n");
        svcSleepThread(2000000000LL);
        return 1;
    }

    const Cube cube;  // client arrays: must outlive every draw that references them
    Texture texture;

    glVertexPointer(3, GL_FLOAT, sizeof(Vertex), &cube.vertices[0].position);
    glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), &cube.vertices[0].uv);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), &cube.vertices[0].color);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1, 1, -.6, .6, 1, 10);  // 400x240 top screen is 5:3
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glClearColor(.09f, .14f, .24f, 1);

    float angle = 0;
    unsigned frame = 0;
    bool linear = false;
    while (aptMainLoop()) {
        hidScanInput();
        const u32 down = hidKeysDown();
        if (down & KEY_START) break;
        if (down & KEY_A) {
            linear = !linear;
            texture.setLinear(linear);
        }

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glLoadIdentity();
        glTranslatef(0, 0, -2.6f);
        glRotatef(30, 1, 0, 0);
        glRotatef(angle, 0, 1, 0);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(cube.indices.size()), GL_UNSIGNED_SHORT, cube.indices.data());

        if (c3dglNativePresent() != 0 || glGetError() != GL_NO_ERROR) {
            std::printf("frame %u failed\n", frame);
            return 1;
        }

        if (frame++ % 30 == 0) {
            C3DGLnativeStats stats;
            if (c3dglNativeGetStats(&stats) == 0)
                std::printf("\x1b[1;1Hc3dgl C++ example\nCPU %6.3f ms  draws %lu   \n%s filtering\n\nA: toggle filter\nSTART: exit\n",
                            static_cast<double>(stats.cpu_ticks) * 1000 / SYSCLOCK_ARM11,
                            static_cast<unsigned long>(stats.draws), linear ? "linear " : "nearest");
        }
        angle += .8f;
        if (angle >= 360) angle -= 360;
    }
    return 0;
}

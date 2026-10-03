// c3dgl: OpenGL 1.1 subset for the Nintendo 3DS, implemented on citro3d (src/c3dgl.c).
//
// Put c3dgl/include on the include path and code written against <GL/gl.h> picks up this file.
// Standard OpenGL enum values; only the declared functions exist (see c3dgl.h for the list of
// what is supported and what is not). Deliberately free of libctru/citro3d includes, so it can be
// used next to headers that clash with <3ds.h> (e.g. raylib.h: KEY_A, ...).
#ifndef C3DGL_GL_H
#define C3DGL_GL_H

#ifdef __cplusplus
extern "C" {
#endif

// Calling convention macros as defined by every gl.h (empty on the 3DS)
#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef GLAPIENTRY
#define GLAPIENTRY APIENTRY
#endif
#ifndef GLAPI
#define GLAPI extern
#endif

typedef unsigned int    GLenum;
typedef unsigned int    GLbitfield;
typedef unsigned int    GLuint;
typedef int             GLint;
typedef int             GLsizei;
typedef unsigned char   GLboolean;
typedef signed char     GLbyte;
typedef short           GLshort;
typedef unsigned char   GLubyte;
typedef unsigned short  GLushort;
typedef float           GLfloat;
typedef float           GLclampf;
typedef double          GLdouble;
typedef double          GLclampd;
typedef long            GLintptr;       // Buffer offsets and sizes (pointer sized)
typedef long            GLsizeiptr;
typedef int             GLfixed;        // OpenGL ES 16.16 fixed point
typedef int             GLclampx;
typedef void            GLvoid;

// Boolean
#define GL_FALSE                            0
#define GL_TRUE                             1

// Errors
#define GL_NO_ERROR                         0
#define GL_INVALID_ENUM                     0x0500
#define GL_INVALID_VALUE                    0x0501
#define GL_INVALID_OPERATION                0x0502
#define GL_STACK_OVERFLOW                   0x0503
#define GL_STACK_UNDERFLOW                  0x0504
#define GL_OUT_OF_MEMORY                    0x0505

// Primitives
#define GL_POINTS                           0x0000
#define GL_LINES                            0x0001
#define GL_LINE_LOOP                        0x0002
#define GL_LINE_STRIP                       0x0003
#define GL_TRIANGLES                        0x0004
#define GL_TRIANGLE_STRIP                   0x0005
#define GL_TRIANGLE_FAN                     0x0006
#define GL_QUADS                            0x0007
#define GL_QUAD_STRIP                       0x0008
#define GL_POLYGON                          0x0009

// Clear buffer bits, also attribute bits (glPushAttrib)
#define GL_CURRENT_BIT                      0x00000001
#define GL_POINT_BIT                        0x00000002
#define GL_LINE_BIT                         0x00000004
#define GL_POLYGON_BIT                      0x00000008
#define GL_POLYGON_STIPPLE_BIT              0x00000010
#define GL_PIXEL_MODE_BIT                   0x00000020
#define GL_LIGHTING_BIT                     0x00000040
#define GL_FOG_BIT                          0x00000080
#define GL_DEPTH_BUFFER_BIT                 0x00000100
#define GL_ACCUM_BUFFER_BIT                 0x00000200
#define GL_STENCIL_BUFFER_BIT               0x00000400
#define GL_VIEWPORT_BIT                     0x00000800
#define GL_TRANSFORM_BIT                    0x00001000
#define GL_ENABLE_BIT                       0x00002000
#define GL_COLOR_BUFFER_BIT                 0x00004000
#define GL_HINT_BIT                         0x00008000
#define GL_EVAL_BIT                         0x00010000
#define GL_LIST_BIT                         0x00020000
#define GL_TEXTURE_BIT                      0x00040000
#define GL_SCISSOR_BIT                      0x00080000
#define GL_ALL_ATTRIB_BITS                  0x000FFFFF
#define GL_CLIENT_PIXEL_STORE_BIT           0x00000001
#define GL_CLIENT_VERTEX_ARRAY_BIT          0x00000002
#define GL_CLIENT_ALL_ATTRIB_BITS           0xFFFFFFFF

// Depth/compare functions
#define GL_NEVER                            0x0200
#define GL_LESS                             0x0201
#define GL_EQUAL                            0x0202
#define GL_LEQUAL                           0x0203
#define GL_GREATER                          0x0204
#define GL_NOTEQUAL                         0x0205
#define GL_GEQUAL                           0x0206
#define GL_ALWAYS                           0x0207

// Stencil operations
#define GL_KEEP                             0x1E00
#define GL_INCR                             0x1E02
#define GL_DECR                             0x1E03
#define GL_INVERT                           0x150A
#define GL_INCR_WRAP                        0x8507
#define GL_DECR_WRAP                        0x8508

// Blend factors
#define GL_ZERO                             0
#define GL_ONE                              1
#define GL_SRC_COLOR                        0x0300
#define GL_ONE_MINUS_SRC_COLOR              0x0301
#define GL_SRC_ALPHA                        0x0302
#define GL_ONE_MINUS_SRC_ALPHA              0x0303
#define GL_DST_ALPHA                        0x0304
#define GL_ONE_MINUS_DST_ALPHA              0x0305
#define GL_DST_COLOR                        0x0306
#define GL_ONE_MINUS_DST_COLOR              0x0307
#define GL_SRC_ALPHA_SATURATE               0x0308

// Faces / winding / polygon mode
#define GL_FRONT                            0x0404
#define GL_BACK                             0x0405
#define GL_FRONT_AND_BACK                   0x0408
#define GL_CW                               0x0900
#define GL_CCW                              0x0901
#define GL_POINT                            0x1B00
#define GL_LINE                             0x1B01
#define GL_FILL                             0x1B02

// Capabilities (glEnable/glDisable)
#define GL_LINE_SMOOTH                      0x0B20
#define GL_CULL_FACE                        0x0B44
#define GL_DEPTH_TEST                       0x0B71
#define GL_ALPHA_TEST                       0x0BC0
#define GL_STENCIL_TEST                     0x0B90
#define GL_BLEND                            0x0BE2
#define GL_SCISSOR_TEST                     0x0C11
#define GL_TEXTURE_2D                       0x0DE1
#define GL_POLYGON_OFFSET_POINT             0x2A01
#define GL_POLYGON_OFFSET_LINE              0x2A02
#define GL_POLYGON_OFFSET_FILL              0x8037
#define GL_LIGHTING                         0x0B50
#define GL_FOG                              0x0B60
#define GL_COLOR_MATERIAL                   0x0B57
#define GL_NORMALIZE                        0x0BA1
#define GL_RESCALE_NORMAL                   0x803A      // ES, GL 1.2
#define GL_LIGHT0                           0x4000
#define GL_LIGHT1                           0x4001
#define GL_LIGHT2                           0x4002
#define GL_LIGHT3                           0x4003
#define GL_LIGHT4                           0x4004
#define GL_LIGHT5                           0x4005
#define GL_LIGHT6                           0x4006
#define GL_LIGHT7                           0x4007

// Capabilities that are accepted and reported by glIsEnabled, but have no effect (enabling the
// non-cosmetic ones logs a warning)
#define GL_POINT_SMOOTH                     0x0B10
#define GL_POLYGON_SMOOTH                   0x0B41
#define GL_DITHER                           0x0BD0

// Fog (glFog)
#define GL_FOG_INDEX                        0x0B61      // Color index mode: stored only
#define GL_FOG_DENSITY                      0x0B62
#define GL_FOG_START                        0x0B63
#define GL_FOG_END                          0x0B64
#define GL_FOG_MODE                         0x0B65
#define GL_FOG_COLOR                        0x0B66
#define GL_EXP                              0x0800
#define GL_EXP2                             0x0801
#define GL_FOG_HINT                         0x0C54

// User clip planes (glClipPlane)
#define GL_CLIP_PLANE0                      0x3000
#define GL_CLIP_PLANE1                      0x3001
#define GL_CLIP_PLANE2                      0x3002
#define GL_CLIP_PLANE3                      0x3003
#define GL_CLIP_PLANE4                      0x3004
#define GL_CLIP_PLANE5                      0x3005
#define GL_MAX_CLIP_PLANES                  0x0D32

// Lighting (glLight, glLightModel, glMaterial, glColorMaterial)
#define GL_AMBIENT                          0x1200
#define GL_DIFFUSE                          0x1201
#define GL_SPECULAR                         0x1202
#define GL_POSITION                         0x1203
#define GL_SPOT_DIRECTION                   0x1204
#define GL_SPOT_EXPONENT                    0x1205
#define GL_SPOT_CUTOFF                      0x1206
#define GL_CONSTANT_ATTENUATION             0x1207
#define GL_LINEAR_ATTENUATION               0x1208
#define GL_QUADRATIC_ATTENUATION            0x1209
#define GL_EMISSION                         0x1600
#define GL_SHININESS                        0x1601
#define GL_AMBIENT_AND_DIFFUSE              0x1602
#define GL_COLOR_INDEXES                    0x1603      // Color index mode: stored only
#define GL_LIGHT_MODEL_LOCAL_VIEWER         0x0B51
#define GL_LIGHT_MODEL_TWO_SIDE             0x0B52
#define GL_LIGHT_MODEL_AMBIENT              0x0B53
#define GL_COLOR_MATERIAL_FACE              0x0B55
#define GL_COLOR_MATERIAL_PARAMETER         0x0B56
#define GL_MAX_LIGHTS                       0x0D31

// Client arrays
#define GL_VERTEX_ARRAY                     0x8074
#define GL_NORMAL_ARRAY                     0x8075
#define GL_COLOR_ARRAY                      0x8076
#define GL_EDGE_FLAG_ARRAY                  0x8079
#define GL_VERTEX_ARRAY_SIZE                0x807A
#define GL_VERTEX_ARRAY_TYPE                0x807B
#define GL_VERTEX_ARRAY_STRIDE              0x807C
#define GL_NORMAL_ARRAY_TYPE                0x807E
#define GL_NORMAL_ARRAY_STRIDE              0x807F
#define GL_COLOR_ARRAY_SIZE                 0x8081
#define GL_COLOR_ARRAY_TYPE                 0x8082
#define GL_COLOR_ARRAY_STRIDE               0x8083
#define GL_TEXTURE_COORD_ARRAY_SIZE         0x8088
#define GL_TEXTURE_COORD_ARRAY_TYPE         0x8089
#define GL_TEXTURE_COORD_ARRAY_STRIDE       0x808A
#define GL_EDGE_FLAG_ARRAY_STRIDE           0x808C
#define GL_VERTEX_ARRAY_POINTER             0x808E
#define GL_NORMAL_ARRAY_POINTER             0x808F
#define GL_COLOR_ARRAY_POINTER              0x8090
#define GL_TEXTURE_COORD_ARRAY_POINTER      0x8092
#define GL_EDGE_FLAG_ARRAY_POINTER          0x8093

// Buffer objects (ES 1.1, GL 1.5)
#define GL_ARRAY_BUFFER                     0x8892
#define GL_ELEMENT_ARRAY_BUFFER             0x8893
#define GL_ARRAY_BUFFER_BINDING             0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING     0x8895
#define GL_VERTEX_ARRAY_BUFFER_BINDING      0x8896
#define GL_NORMAL_ARRAY_BUFFER_BINDING      0x8897
#define GL_COLOR_ARRAY_BUFFER_BINDING       0x8898
#define GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING 0x889A
#define GL_EDGE_FLAG_ARRAY_BUFFER_BINDING   0x889B
#define GL_STREAM_DRAW                      0x88E0
#define GL_STREAM_READ                      0x88E1
#define GL_STREAM_COPY                      0x88E2
#define GL_STATIC_DRAW                      0x88E4
#define GL_STATIC_READ                      0x88E5
#define GL_STATIC_COPY                      0x88E6
#define GL_DYNAMIC_DRAW                     0x88E8
#define GL_DYNAMIC_READ                     0x88E9
#define GL_DYNAMIC_COPY                     0x88EA
#define GL_BUFFER_SIZE                      0x8764
#define GL_BUFFER_USAGE                     0x8765

// glInterleavedArrays formats
#define GL_V2F                              0x2A20
#define GL_V3F                              0x2A21
#define GL_C4UB_V2F                         0x2A22
#define GL_C4UB_V3F                         0x2A23
#define GL_C3F_V3F                          0x2A24
#define GL_N3F_V3F                          0x2A25
#define GL_C4F_N3F_V3F                      0x2A26
#define GL_T2F_V3F                          0x2A27
#define GL_T4F_V4F                          0x2A28
#define GL_T2F_C4UB_V3F                     0x2A29
#define GL_T2F_C3F_V3F                      0x2A2A
#define GL_T2F_N3F_V3F                      0x2A2B
#define GL_T2F_C4F_N3F_V3F                  0x2A2C
#define GL_T4F_C4F_N3F_V4F                  0x2A2D
#define GL_TEXTURE_COORD_ARRAY              0x8078

// Queries (glGet*)
#define GL_CURRENT_COLOR                    0x0B00
#define GL_CURRENT_NORMAL                   0x0B02
#define GL_CURRENT_TEXTURE_COORDS           0x0B03
#define GL_POINT_SIZE                       0x0B11
#define GL_POINT_SIZE_RANGE                 0x0B12
#define GL_SMOOTH_POINT_SIZE_RANGE          0x0B12
#define GL_POINT_SIZE_GRANULARITY           0x0B13
#define GL_ALIASED_POINT_SIZE_RANGE         0x846D
#define GL_LINE_WIDTH                       0x0B21
#define GL_CULL_FACE_MODE                   0x0B45
#define GL_FRONT_FACE                       0x0B46
#define GL_SHADE_MODEL                      0x0B54
#define GL_DEPTH_RANGE                      0x0B70
#define GL_POLYGON_MODE                     0x0B40
#define GL_EDGE_FLAG                        0x0B43
#define GL_POLYGON_OFFSET_UNITS             0x2A00
#define GL_POLYGON_OFFSET_FACTOR            0x8038
#define GL_DEPTH_WRITEMASK                  0x0B72
#define GL_DEPTH_CLEAR_VALUE                0x0B73
#define GL_DEPTH_FUNC                       0x0B74
#define GL_STENCIL_CLEAR_VALUE              0x0B91
#define GL_STENCIL_FUNC                     0x0B92
#define GL_STENCIL_VALUE_MASK               0x0B93
#define GL_STENCIL_FAIL                     0x0B94
#define GL_STENCIL_PASS_DEPTH_FAIL          0x0B95
#define GL_STENCIL_PASS_DEPTH_PASS          0x0B96
#define GL_STENCIL_REF                      0x0B97
#define GL_STENCIL_WRITEMASK                0x0B98
#define GL_MATRIX_MODE                      0x0BA0
#define GL_VIEWPORT                         0x0BA2
#define GL_MODELVIEW_STACK_DEPTH            0x0BA3
#define GL_PROJECTION_STACK_DEPTH           0x0BA4
#define GL_TEXTURE_STACK_DEPTH              0x0BA5
#define GL_MODELVIEW_MATRIX                 0x0BA6
#define GL_PROJECTION_MATRIX                0x0BA7
#define GL_TEXTURE_MATRIX                   0x0BA8
#define GL_ALPHA_TEST_FUNC                  0x0BC1
#define GL_ALPHA_TEST_REF                   0x0BC2
#define GL_BLEND_DST                        0x0BE0
#define GL_BLEND_SRC                        0x0BE1
#define GL_SCISSOR_BOX                      0x0C10
#define GL_COLOR_CLEAR_VALUE                0x0C22
#define GL_COLOR_WRITEMASK                  0x0C23
#define GL_MAX_TEXTURE_SIZE                 0x0D33
#define GL_MAX_MODELVIEW_STACK_DEPTH        0x0D36
#define GL_MAX_PROJECTION_STACK_DEPTH       0x0D38
#define GL_MAX_TEXTURE_STACK_DEPTH          0x0D39
#define GL_MAX_VIEWPORT_DIMS                0x0D3A
#define GL_ATTRIB_STACK_DEPTH               0x0BB0
#define GL_CLIENT_ATTRIB_STACK_DEPTH        0x0BB1
#define GL_MAX_ATTRIB_STACK_DEPTH           0x0D35
#define GL_MAX_CLIENT_ATTRIB_STACK_DEPTH    0x0D3B
#define GL_RED_BITS                         0x0D52
#define GL_GREEN_BITS                       0x0D53
#define GL_BLUE_BITS                        0x0D54
#define GL_ALPHA_BITS                       0x0D55
#define GL_DEPTH_BITS                       0x0D56
#define GL_STENCIL_BITS                     0x0D57
#define GL_TEXTURE_BINDING_2D               0x8069
#define GL_VENDOR                           0x1F00
#define GL_RENDERER                         0x1F01
#define GL_VERSION                          0x1F02
#define GL_EXTENSIONS                       0x1F03

// Hints / shading
#define GL_PERSPECTIVE_CORRECTION_HINT      0x0C50
#define GL_DONT_CARE                        0x1100
#define GL_FASTEST                          0x1101
#define GL_NICEST                           0x1102
#define GL_FLAT                             0x1D00
#define GL_SMOOTH                           0x1D01

// Matrix modes
#define GL_MODELVIEW                        0x1700
#define GL_PROJECTION                       0x1701
#define GL_TEXTURE                          0x1702

// Pixel store
#define GL_UNPACK_SWAP_BYTES                0x0CF0
#define GL_UNPACK_LSB_FIRST                 0x0CF1
#define GL_UNPACK_ROW_LENGTH                0x0CF2
#define GL_UNPACK_SKIP_ROWS                 0x0CF3
#define GL_UNPACK_SKIP_PIXELS               0x0CF4
#define GL_UNPACK_ALIGNMENT                 0x0CF5
#define GL_PACK_SWAP_BYTES                  0x0D00
#define GL_PACK_LSB_FIRST                   0x0D01
#define GL_PACK_ROW_LENGTH                  0x0D02
#define GL_PACK_SKIP_ROWS                   0x0D03
#define GL_PACK_SKIP_PIXELS                 0x0D04
#define GL_PACK_ALIGNMENT                   0x0D05
#define GL_PACK_SKIP_IMAGES                 0x806B      // GL 1.2, accepted for GLU
#define GL_PACK_IMAGE_HEIGHT                0x806C
#define GL_UNPACK_SKIP_IMAGES               0x806D
#define GL_UNPACK_IMAGE_HEIGHT              0x806E

// Data types
#define GL_BYTE                             0x1400
#define GL_UNSIGNED_BYTE                    0x1401
#define GL_SHORT                            0x1402
#define GL_UNSIGNED_SHORT                   0x1403
#define GL_INT                              0x1404
#define GL_UNSIGNED_INT                     0x1405
#define GL_FLOAT                            0x1406
#define GL_DOUBLE                           0x140A
#define GL_FIXED                            0x140C      // ES
#define GL_UNSIGNED_SHORT_4_4_4_4           0x8033
#define GL_UNSIGNED_SHORT_5_5_5_1           0x8034
#define GL_UNSIGNED_SHORT_5_6_5             0x8363
#define GL_BITMAP                           0x1A00

// GL 1.2 packed pixel types: not accepted by c3dgl, defined for code like GLU that handles them itself
#define GL_UNSIGNED_BYTE_3_3_2              0x8032
#define GL_UNSIGNED_INT_8_8_8_8             0x8035
#define GL_UNSIGNED_INT_10_10_10_2          0x8036
#define GL_UNSIGNED_BYTE_2_3_3_REV          0x8362
#define GL_UNSIGNED_SHORT_5_6_5_REV         0x8364
#define GL_UNSIGNED_SHORT_4_4_4_4_REV       0x8365
#define GL_UNSIGNED_SHORT_1_5_5_5_REV       0x8366
#define GL_UNSIGNED_INT_8_8_8_8_REV         0x8367
#define GL_UNSIGNED_INT_2_10_10_10_REV      0x8368

// Pixel formats
#define GL_COLOR_INDEX                      0x1900
#define GL_STENCIL_INDEX                    0x1901
#define GL_DEPTH_COMPONENT                  0x1902
#define GL_RED                              0x1903
#define GL_GREEN                            0x1904
#define GL_BLUE                             0x1905
#define GL_ALPHA                            0x1906
#define GL_RGB                              0x1907
#define GL_RGBA                             0x1908
#define GL_LUMINANCE                        0x1909
#define GL_LUMINANCE_ALPHA                  0x190A
#define GL_BGR                              0x80E0      // GL 1.2, defined for GLU
#define GL_BGRA                             0x80E1

// Multitexturing (ES 1.1, GL 1.3): units GL_TEXTURE0 + n
#define GL_TEXTURE0                         0x84C0
#define GL_TEXTURE1                         0x84C1
#define GL_TEXTURE2                         0x84C2
#define GL_ACTIVE_TEXTURE                   0x84E0
#define GL_CLIENT_ACTIVE_TEXTURE            0x84E1
#define GL_MAX_TEXTURE_UNITS                0x84E2

// Texture environment
#define GL_TEXTURE_ENV                      0x2300
#define GL_TEXTURE_ENV_MODE                 0x2200
#define GL_TEXTURE_ENV_COLOR                0x2201
#define GL_MODULATE                         0x2100
#define GL_DECAL                            0x2101
#define GL_REPLACE                          0x1E01
#define GL_ADD                              0x0104

// Texture combiners (GL_COMBINE: ES 1.1, GL 1.3)
#define GL_COMBINE                          0x8570
#define GL_COMBINE_RGB                      0x8571
#define GL_COMBINE_ALPHA                    0x8572
#define GL_RGB_SCALE                        0x8573
#define GL_ADD_SIGNED                       0x8574
#define GL_INTERPOLATE                      0x8575
#define GL_CONSTANT                         0x8576
#define GL_PRIMARY_COLOR                    0x8577
#define GL_PREVIOUS                         0x8578
#define GL_SUBTRACT                         0x84E7
#define GL_DOT3_RGB                         0x86AE
#define GL_DOT3_RGBA                        0x86AF
#define GL_ALPHA_SCALE                      0x0D1C
#define GL_SRC0_RGB                         0x8580
#define GL_SRC1_RGB                         0x8581
#define GL_SRC2_RGB                         0x8582
#define GL_SRC0_ALPHA                       0x8588
#define GL_SRC1_ALPHA                       0x8589
#define GL_SRC2_ALPHA                       0x858A
#define GL_SOURCE0_RGB                      GL_SRC0_RGB     // GL 1.3 names
#define GL_SOURCE1_RGB                      GL_SRC1_RGB
#define GL_SOURCE2_RGB                      GL_SRC2_RGB
#define GL_SOURCE0_ALPHA                    GL_SRC0_ALPHA
#define GL_SOURCE1_ALPHA                    GL_SRC1_ALPHA
#define GL_SOURCE2_ALPHA                    GL_SRC2_ALPHA
#define GL_OPERAND0_RGB                     0x8590
#define GL_OPERAND1_RGB                     0x8591
#define GL_OPERAND2_RGB                     0x8592
#define GL_OPERAND0_ALPHA                   0x8598
#define GL_OPERAND1_ALPHA                   0x8599
#define GL_OPERAND2_ALPHA                   0x859A

// Texture targets
#define GL_TEXTURE_1D                       0x0DE0
#define GL_PROXY_TEXTURE_1D                 0x8063
#define GL_PROXY_TEXTURE_2D                 0x8064
#define GL_TEXTURE_3D                       0x806F      // GL 1.2, defined for GLU
#define GL_PROXY_TEXTURE_3D                 0x8070

// Texture level parameters (glGetTexLevelParameter)
#define GL_TEXTURE_WIDTH                    0x1000
#define GL_TEXTURE_HEIGHT                   0x1001
#define GL_TEXTURE_INTERNAL_FORMAT          0x1003
#define GL_TEXTURE_COMPONENTS               0x1003
#define GL_TEXTURE_BORDER                   0x1005
#define GL_TEXTURE_RED_SIZE                 0x805C
#define GL_TEXTURE_GREEN_SIZE               0x805D
#define GL_TEXTURE_BLUE_SIZE                0x805E
#define GL_TEXTURE_ALPHA_SIZE               0x805F
#define GL_TEXTURE_LUMINANCE_SIZE           0x8060
#define GL_TEXTURE_INTENSITY_SIZE           0x8061

// Evaluators
#define GL_AUTO_NORMAL                      0x0D80
#define GL_MAP1_COLOR_4                     0x0D90
#define GL_MAP1_INDEX                       0x0D91
#define GL_MAP1_NORMAL                      0x0D92
#define GL_MAP1_TEXTURE_COORD_1             0x0D93
#define GL_MAP1_TEXTURE_COORD_2             0x0D94
#define GL_MAP1_TEXTURE_COORD_3             0x0D95
#define GL_MAP1_TEXTURE_COORD_4             0x0D96
#define GL_MAP1_VERTEX_3                    0x0D97
#define GL_MAP1_VERTEX_4                    0x0D98
#define GL_MAP2_COLOR_4                     0x0DB0
#define GL_MAP2_INDEX                       0x0DB1
#define GL_MAP2_NORMAL                      0x0DB2
#define GL_MAP2_TEXTURE_COORD_1             0x0DB3
#define GL_MAP2_TEXTURE_COORD_2             0x0DB4
#define GL_MAP2_TEXTURE_COORD_3             0x0DB5
#define GL_MAP2_TEXTURE_COORD_4             0x0DB6
#define GL_MAP2_VERTEX_3                    0x0DB7
#define GL_MAP2_VERTEX_4                    0x0DB8
#define GL_MAP1_GRID_DOMAIN                 0x0DD0
#define GL_MAP1_GRID_SEGMENTS               0x0DD1
#define GL_MAP2_GRID_DOMAIN                 0x0DD2
#define GL_MAP2_GRID_SEGMENTS               0x0DD3
#define GL_MAX_EVAL_ORDER                   0x0D30
#define GL_COEFF                            0x0A00
#define GL_ORDER                            0x0A01
#define GL_DOMAIN                           0x0A02

// Texture parameters
#define GL_TEXTURE_MAG_FILTER               0x2800
#define GL_TEXTURE_MIN_FILTER               0x2801
#define GL_TEXTURE_WRAP_S                   0x2802
#define GL_TEXTURE_WRAP_T                   0x2803
#define GL_NEAREST                          0x2600
#define GL_LINEAR                           0x2601
#define GL_NEAREST_MIPMAP_NEAREST           0x2700
#define GL_LINEAR_MIPMAP_NEAREST            0x2701
#define GL_NEAREST_MIPMAP_LINEAR            0x2702
#define GL_LINEAR_MIPMAP_LINEAR             0x2703
#define GL_CLAMP                            0x2900      // GL; treated like GL_CLAMP_TO_EDGE
#define GL_REPEAT                           0x2901
#define GL_GENERATE_MIPMAP                  0x8191      // ES
#define GL_GENERATE_MIPMAP_HINT             0x8192      // ES
#define GL_CLAMP_TO_EDGE                    0x812F
#define GL_MIRRORED_REPEAT                  0x8370

// State
void glEnable(GLenum cap);
void glDisable(GLenum cap);
GLboolean glIsEnabled(GLenum cap);
void glEnableClientState(GLenum array);
void glDisableClientState(GLenum array);
void glHint(GLenum target, GLenum mode);
void glShadeModel(GLenum mode);
void glPixelStorei(GLenum pname, GLint param);
void glPixelStoref(GLenum pname, GLfloat param);
void glGetBooleanv(GLenum pname, GLboolean *params);
void glGetIntegerv(GLenum pname, GLint *params);
void glGetFloatv(GLenum pname, GLfloat *params);
void glGetDoublev(GLenum pname, GLdouble *params);
const GLubyte *glGetString(GLenum name);
GLenum glGetError(void);
void glFlush(void);                     // Drawing is submitted by c3dglSwapBuffers(); these only flush the batch
void glFinish(void);

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void glClearDepth(GLclampd depth);
void glClearStencil(GLint s);
void glClear(GLbitfield mask);
void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
void glDepthMask(GLboolean flag);
void glDepthFunc(GLenum func);
void glAlphaFunc(GLenum func, GLclampf ref);
void glStencilFunc(GLenum func, GLint ref, GLuint mask);
void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass);
void glStencilMask(GLuint mask);
void glBlendFunc(GLenum sfactor, GLenum dfactor);
void glCullFace(GLenum mode);
void glFrontFace(GLenum mode);
void glPolygonMode(GLenum face, GLenum mode);
void glPolygonOffset(GLfloat factor, GLfloat units);
void glDepthRange(GLclampd zNear, GLclampd zFar);
void glLineWidth(GLfloat width);
void glPointSize(GLfloat size);

// Point parameters (GL 1.4, ES 1.1): size attenuation by eye distance, clamped to GL_POINT_SIZE_MIN/MAX
#define GL_POINT_SIZE_MIN                   0x8126
#define GL_POINT_SIZE_MAX                   0x8127
#define GL_POINT_FADE_THRESHOLD_SIZE        0x8128
#define GL_POINT_DISTANCE_ATTENUATION       0x8129
void glPointParameterf(GLenum pname, GLfloat param);
void glPointParameterfv(GLenum pname, const GLfloat *params);
void glPointParameteri(GLenum pname, GLint param);
void glPointParameteriv(GLenum pname, const GLint *params);

// Point sprites (ES 1.1 GL_OES_point_sprite, GL 2.0): glEnable(GL_POINT_SPRITE_OES) and per texture unit
// glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_TRUE)
#define GL_POINT_SPRITE_OES                 0x8861
#define GL_COORD_REPLACE_OES                0x8862
#define GL_POINT_SPRITE                     0x8861
#define GL_COORD_REPLACE                    0x8862
#define GL_OES_point_sprite                 1

// Point size array (ES 1.1 GL_OES_point_size_array): per-vertex point sizes instead of glPointSize
#define GL_POINT_SIZE_ARRAY_OES             0x8B9C
#define GL_POINT_SIZE_ARRAY_TYPE_OES        0x898A
#define GL_POINT_SIZE_ARRAY_STRIDE_OES      0x898B
#define GL_POINT_SIZE_ARRAY_POINTER_OES     0x898C
#define GL_POINT_SIZE_ARRAY_BUFFER_BINDING_OES 0x8B9F
#define GL_OES_point_size_array             1
void glPointSizePointerOES(GLenum type, GLsizei stride, const GLvoid *pointer);

// Matrices
void glMatrixMode(GLenum mode);
void glPushMatrix(void);
void glPopMatrix(void);
void glLoadIdentity(void);
void glLoadMatrixf(const GLfloat *m);
void glLoadMatrixd(const GLdouble *m);
void glMultMatrixf(const GLfloat *m);
void glMultMatrixd(const GLdouble *m);
void glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void glTranslated(GLdouble x, GLdouble y, GLdouble z);
void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
void glScalef(GLfloat x, GLfloat y, GLfloat z);
void glScaled(GLdouble x, GLdouble y, GLdouble z);
void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
void glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);

// Immediate mode
void glBegin(GLenum mode);
void glEnd(void);
void glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w);    // Divided by w, which must not be 0
void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q); // r is ignored (2D textures only)
void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz);
void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha);
void glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2);
void glEdgeFlag(GLboolean flag);
void glEdgeFlagv(const GLboolean *flag);

// All other variants of glVertex{2,3,4}, glTexCoord{1,2,3,4}, glNormal3, glColor{3,4}, glRect
void glVertex2d(GLdouble x, GLdouble y);
void glVertex2dv(const GLdouble *v);
void glVertex2f(GLfloat x, GLfloat y);
void glVertex2fv(const GLfloat *v);
void glVertex2i(GLint x, GLint y);
void glVertex2iv(const GLint *v);
void glVertex2s(GLshort x, GLshort y);
void glVertex2sv(const GLshort *v);
void glVertex3d(GLdouble x, GLdouble y, GLdouble z);
void glVertex3dv(const GLdouble *v);
void glVertex3fv(const GLfloat *v);
void glVertex3i(GLint x, GLint y, GLint z);
void glVertex3iv(const GLint *v);
void glVertex3s(GLshort x, GLshort y, GLshort z);
void glVertex3sv(const GLshort *v);
void glVertex4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w);
void glVertex4dv(const GLdouble *v);
void glVertex4fv(const GLfloat *v);
void glVertex4i(GLint x, GLint y, GLint z, GLint w);
void glVertex4iv(const GLint *v);
void glVertex4s(GLshort x, GLshort y, GLshort z, GLshort w);
void glVertex4sv(const GLshort *v);
void glTexCoord1d(GLdouble s);
void glTexCoord1dv(const GLdouble *v);
void glTexCoord1f(GLfloat s);
void glTexCoord1fv(const GLfloat *v);
void glTexCoord1i(GLint s);
void glTexCoord1iv(const GLint *v);
void glTexCoord1s(GLshort s);
void glTexCoord1sv(const GLshort *v);
void glTexCoord2d(GLdouble s, GLdouble t);
void glTexCoord2dv(const GLdouble *v);
void glTexCoord2f(GLfloat s, GLfloat t);
void glTexCoord2fv(const GLfloat *v);
void glTexCoord2i(GLint s, GLint t);
void glTexCoord2iv(const GLint *v);
void glTexCoord2s(GLshort s, GLshort t);
void glTexCoord2sv(const GLshort *v);
void glTexCoord3d(GLdouble s, GLdouble t, GLdouble r);
void glTexCoord3dv(const GLdouble *v);
void glTexCoord3f(GLfloat s, GLfloat t, GLfloat r);
void glTexCoord3fv(const GLfloat *v);
void glTexCoord3i(GLint s, GLint t, GLint r);
void glTexCoord3iv(const GLint *v);
void glTexCoord3s(GLshort s, GLshort t, GLshort r);
void glTexCoord3sv(const GLshort *v);
void glTexCoord4d(GLdouble s, GLdouble t, GLdouble r, GLdouble q);
void glTexCoord4dv(const GLdouble *v);
void glTexCoord4fv(const GLfloat *v);
void glTexCoord4i(GLint s, GLint t, GLint r, GLint q);
void glTexCoord4iv(const GLint *v);
void glTexCoord4s(GLshort s, GLshort t, GLshort r, GLshort q);
void glTexCoord4sv(const GLshort *v);
void glNormal3b(GLbyte nx, GLbyte ny, GLbyte nz);
void glNormal3bv(const GLbyte *v);
void glNormal3d(GLdouble nx, GLdouble ny, GLdouble nz);
void glNormal3dv(const GLdouble *v);
void glNormal3fv(const GLfloat *v);
void glNormal3i(GLint nx, GLint ny, GLint nz);
void glNormal3iv(const GLint *v);
void glNormal3s(GLshort nx, GLshort ny, GLshort nz);
void glNormal3sv(const GLshort *v);
void glColor3b(GLbyte red, GLbyte green, GLbyte blue);
void glColor3bv(const GLbyte *v);
void glColor3d(GLdouble red, GLdouble green, GLdouble blue);
void glColor3dv(const GLdouble *v);
void glColor3f(GLfloat red, GLfloat green, GLfloat blue);
void glColor3fv(const GLfloat *v);
void glColor3i(GLint red, GLint green, GLint blue);
void glColor3iv(const GLint *v);
void glColor3s(GLshort red, GLshort green, GLshort blue);
void glColor3sv(const GLshort *v);
void glColor3ub(GLubyte red, GLubyte green, GLubyte blue);
void glColor3ubv(const GLubyte *v);
void glColor3ui(GLuint red, GLuint green, GLuint blue);
void glColor3uiv(const GLuint *v);
void glColor3us(GLushort red, GLushort green, GLushort blue);
void glColor3usv(const GLushort *v);
void glColor4b(GLbyte red, GLbyte green, GLbyte blue, GLbyte alpha);
void glColor4bv(const GLbyte *v);
void glColor4d(GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha);
void glColor4dv(const GLdouble *v);
void glColor4fv(const GLfloat *v);
void glColor4i(GLint red, GLint green, GLint blue, GLint alpha);
void glColor4iv(const GLint *v);
void glColor4s(GLshort red, GLshort green, GLshort blue, GLshort alpha);
void glColor4sv(const GLshort *v);
void glColor4ubv(const GLubyte *v);
void glColor4ui(GLuint red, GLuint green, GLuint blue, GLuint alpha);
void glColor4uiv(const GLuint *v);
void glColor4us(GLushort red, GLushort green, GLushort blue, GLushort alpha);
void glColor4usv(const GLushort *v);
void glRectd(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2);
void glRectdv(const GLdouble *v1, const GLdouble *v2);
void glRectfv(const GLfloat *v1, const GLfloat *v2);
void glRecti(GLint x1, GLint y1, GLint x2, GLint y2);
void glRectiv(const GLint *v1, const GLint *v2);
void glRects(GLshort x1, GLshort y1, GLshort x2, GLshort y2);
void glRectsv(const GLshort *v1, const GLshort *v2);

// Client-side vertex arrays
void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer);
void glEdgeFlagPointer(GLsizei stride, const GLvoid *pointer);
void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void glInterleavedArrays(GLenum format, GLsizei stride, const GLvoid *pointer);
void glArrayElement(GLint i);
void glGetPointerv(GLenum pname, GLvoid **params);

// Buffer objects (ES 1.1). With a buffer bound to GL_ARRAY_BUFFER, gl*Pointer takes an offset into it; with one
// bound to GL_ELEMENT_ARRAY_BUFFER, glDrawElements takes an offset for the indices
void glGenBuffers(GLsizei n, GLuint *buffers);
void glDeleteBuffers(GLsizei n, const GLuint *buffers);
void glBindBuffer(GLenum target, GLuint buffer);
GLboolean glIsBuffer(GLuint buffer);
void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage);
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data);
void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);

// Textures
void glGenTextures(GLsizei n, GLuint *textures);
void glDeleteTextures(GLsizei n, const GLuint *textures);
void glBindTexture(GLenum target, GLuint texture);
GLboolean glIsTexture(GLuint texture);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
void glTexParameterf(GLenum target, GLenum pname, GLfloat param);
void glTexParameteriv(GLenum target, GLenum pname, const GLint *params);
void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params);
void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params);
void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params);
void glTexEnvi(GLenum target, GLenum pname, GLint param);
void glTexEnvf(GLenum target, GLenum pname, GLfloat param);
void glTexEnviv(GLenum target, GLenum pname, const GLint *params);
void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params);
void glGetTexEnviv(GLenum target, GLenum pname, GLint *params);
void glGetTexEnvfv(GLenum target, GLenum pname, GLfloat *params);

// Multitexturing (ES 1.1, GL 1.3): 3 units. glTexEnv, glBindTexture, glEnable(GL_TEXTURE_2D), the texture
// matrix and glGet affect the active unit; glTexCoordPointer and GL_TEXTURE_COORD_ARRAY the client active unit
void glActiveTexture(GLenum texture);
void glClientActiveTexture(GLenum texture);
void glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
void glMultiTexCoord1d(GLenum target, GLdouble s);
void glMultiTexCoord1dv(GLenum target, const GLdouble *v);
void glMultiTexCoord1f(GLenum target, GLfloat s);
void glMultiTexCoord1fv(GLenum target, const GLfloat *v);
void glMultiTexCoord1i(GLenum target, GLint s);
void glMultiTexCoord1iv(GLenum target, const GLint *v);
void glMultiTexCoord1s(GLenum target, GLshort s);
void glMultiTexCoord1sv(GLenum target, const GLshort *v);
void glMultiTexCoord2d(GLenum target, GLdouble s, GLdouble t);
void glMultiTexCoord2dv(GLenum target, const GLdouble *v);
void glMultiTexCoord2f(GLenum target, GLfloat s, GLfloat t);
void glMultiTexCoord2fv(GLenum target, const GLfloat *v);
void glMultiTexCoord2i(GLenum target, GLint s, GLint t);
void glMultiTexCoord2iv(GLenum target, const GLint *v);
void glMultiTexCoord2s(GLenum target, GLshort s, GLshort t);
void glMultiTexCoord2sv(GLenum target, const GLshort *v);
void glMultiTexCoord3d(GLenum target, GLdouble s, GLdouble t, GLdouble r);
void glMultiTexCoord3dv(GLenum target, const GLdouble *v);
void glMultiTexCoord3f(GLenum target, GLfloat s, GLfloat t, GLfloat r);
void glMultiTexCoord3fv(GLenum target, const GLfloat *v);
void glMultiTexCoord3i(GLenum target, GLint s, GLint t, GLint r);
void glMultiTexCoord3iv(GLenum target, const GLint *v);
void glMultiTexCoord3s(GLenum target, GLshort s, GLshort t, GLshort r);
void glMultiTexCoord3sv(GLenum target, const GLshort *v);
void glMultiTexCoord4d(GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q);
void glMultiTexCoord4dv(GLenum target, const GLdouble *v);
void glMultiTexCoord4fv(GLenum target, const GLfloat *v);
void glMultiTexCoord4i(GLenum target, GLint s, GLint t, GLint r, GLint q);
void glMultiTexCoord4iv(GLenum target, const GLint *v);
void glMultiTexCoord4s(GLenum target, GLshort s, GLshort t, GLshort r, GLshort q);
void glMultiTexCoord4sv(GLenum target, const GLshort *v);
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                  GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
                     GLenum format, GLenum type, const GLvoid *pixels);
void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels);
void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params);
void glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *params);

// Copying the framebuffer into a texture: internal formats GL_ALPHA, GL_LUMINANCE, GL_LUMINANCE_ALPHA, GL_RGB, GL_RGBA;
// glCopyTexSubImage2D keeps the texture's format. Waits for the GPU to finish the draws so far, like glReadPixels
void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height,
                      GLint border);
void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width,
                         GLsizei height);

// Compressed textures (GL 1.3, ES 1.1). Paletted (ES 1.1 GL_OES_compressed_paletted_texture): expanded to the palette's
// format on load, level <= 0 loads levels 0..-level from one image. ETC1 (GL_OES_compressed_ETC1_RGB8_texture):
// sampled natively by PICA. glCompressedTexSubImage2D is GL_INVALID_OPERATION for both, as the extensions require
#define GL_NUM_COMPRESSED_TEXTURE_FORMATS   0x86A2
#define GL_COMPRESSED_TEXTURE_FORMATS       0x86A3
#define GL_PALETTE4_RGB8_OES                0x8B90
#define GL_PALETTE4_RGBA8_OES               0x8B91
#define GL_PALETTE4_R5_G6_B5_OES            0x8B92
#define GL_PALETTE4_RGBA4_OES               0x8B93
#define GL_PALETTE4_RGB5_A1_OES             0x8B94
#define GL_PALETTE8_RGB8_OES                0x8B95
#define GL_PALETTE8_RGBA8_OES               0x8B96
#define GL_PALETTE8_R5_G6_B5_OES            0x8B97
#define GL_PALETTE8_RGBA4_OES               0x8B98
#define GL_PALETTE8_RGB5_A1_OES             0x8B99
#define GL_ETC1_RGB8_OES                    0x8D64
#define GL_OES_compressed_paletted_texture  1
#define GL_OES_compressed_ETC1_RGB8_texture 1
void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height,
                            GLint border, GLsizei imageSize, const GLvoid *data);
void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
                               GLenum format, GLsizei imageSize, const GLvoid *data);

// Attribute stacks (GL), 16 deep
void glPushAttrib(GLbitfield mask);
void glPopAttrib(void);
void glPushClientAttrib(GLbitfield mask);
void glPopClientAttrib(void);

// Lighting: 8 lights, computed per vertex like GL 1.1 (two-sided, local viewer, spot lights, attenuation).
// i/iv: colors are mapped like glColor (most positive integer = 1.0), positions and directions are not
void glLightf(GLenum light, GLenum pname, GLfloat param);
void glLightfv(GLenum light, GLenum pname, const GLfloat *params);
void glLighti(GLenum light, GLenum pname, GLint param);
void glLightiv(GLenum light, GLenum pname, const GLint *params);
void glLightModelf(GLenum pname, GLfloat param);
void glLightModelfv(GLenum pname, const GLfloat *params);
void glLightModeli(GLenum pname, GLint param);
void glLightModeliv(GLenum pname, const GLint *params);
void glMaterialf(GLenum face, GLenum pname, GLfloat param);
void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params);
void glMateriali(GLenum face, GLenum pname, GLint param);
void glMaterialiv(GLenum face, GLenum pname, const GLint *params);
void glColorMaterial(GLenum face, GLenum mode);
void glGetLightfv(GLenum light, GLenum pname, GLfloat *params);
void glGetLightiv(GLenum light, GLenum pname, GLint *params);
void glGetMaterialfv(GLenum face, GLenum pname, GLfloat *params);
void glGetMaterialiv(GLenum face, GLenum pname, GLint *params);

// Fog: GL_LINEAR, GL_EXP, GL_EXP2 per pixel (PICA fog table over the depth buffer), with the eye distance |z_eye|.
// iv: the color is mapped like glColor*i
void glFogf(GLenum pname, GLfloat param);
void glFogfv(GLenum pname, const GLfloat *params);
void glFogi(GLenum pname, GLint param);
void glFogiv(GLenum pname, const GLint *params);

// User clip planes: 6, in eye coordinates (transformed by the inverse modelview of the glClipPlane call);
// clipped on the CPU
void glClipPlane(GLenum plane, const GLdouble *equation);
void glGetClipPlane(GLenum plane, GLdouble *equation);

// Evaluators (GL): order up to 30
void glMap1f(GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order, const GLfloat *points);
void glMap1d(GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble *points);
void glMap2f(GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder,
             GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, const GLfloat *points);
void glMap2d(GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder,
             GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, const GLdouble *points);
void glMapGrid1f(GLint un, GLfloat u1, GLfloat u2);
void glMapGrid1d(GLint un, GLdouble u1, GLdouble u2);
void glMapGrid2f(GLint un, GLfloat u1, GLfloat u2, GLint vn, GLfloat v1, GLfloat v2);
void glMapGrid2d(GLint un, GLdouble u1, GLdouble u2, GLint vn, GLdouble v1, GLdouble v2);
void glEvalCoord1f(GLfloat u);
void glEvalCoord1d(GLdouble u);
void glEvalCoord1fv(const GLfloat *u);
void glEvalCoord1dv(const GLdouble *u);
void glEvalCoord2f(GLfloat u, GLfloat v);
void glEvalCoord2d(GLdouble u, GLdouble v);
void glEvalCoord2fv(const GLfloat *u);
void glEvalCoord2dv(const GLdouble *u);
void glEvalMesh1(GLenum mode, GLint i1, GLint i2);
void glEvalMesh2(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2);
void glEvalPoint1(GLint i);
void glEvalPoint2(GLint i, GLint j);
void glGetMapiv(GLenum target, GLenum query, GLint *v);
void glGetMapfv(GLenum target, GLenum query, GLfloat *v);
void glGetMapdv(GLenum target, GLenum query, GLdouble *v);

// Not implemented yet: these log a warning and set GL_INVALID_OPERATION
void glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border,
                  GLenum format, GLenum type, const GLvoid *pixels);

// GL 1.2, only so that GLU links: always fails with GL_INVALID_ENUM (no 3D textures)
void glTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth,
                  GLint border, GLenum format, GLenum type, const GLvoid *pixels);

// Reading the framebuffer: color (all GL 1.1 formats and types, plus the packed 16-bit types), GL_DEPTH_COMPONENT and
// GL_STENCIL_INDEX. Waits for the GPU to finish the draws so far
#define GL_IMPLEMENTATION_COLOR_READ_TYPE_OES   0x8B9A      // ES 1.1: GL_UNSIGNED_BYTE
#define GL_IMPLEMENTATION_COLOR_READ_FORMAT_OES 0x8B9B      // ES 1.1: GL_RGBA
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);

// OpenGL ES 1.1: float variants and the fixed-point API (more x functions come with their features)
void glOrthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar);
void glFrustumf(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar);
void glDepthRangef(GLclampf zNear, GLclampf zFar);
void glClearDepthf(GLclampf depth);
void glClipPlanef(GLenum plane, const GLfloat *equation);
void glGetClipPlanef(GLenum plane, GLfloat *equation);
void glAlphaFuncx(GLenum func, GLclampx ref);
void glClearColorx(GLclampx red, GLclampx green, GLclampx blue, GLclampx alpha);
void glClearDepthx(GLclampx depth);
void glColor4x(GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha);
void glDepthRangex(GLclampx zNear, GLclampx zFar);
void glFrustumx(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar);
void glGetFixedv(GLenum pname, GLfixed *params);
void glLineWidthx(GLfixed width);
void glLoadMatrixx(const GLfixed *m);
void glMultMatrixx(const GLfixed *m);
void glNormal3x(GLfixed nx, GLfixed ny, GLfixed nz);
void glOrthox(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar);
void glPointSizex(GLfixed size);
void glPointParameterx(GLenum pname, GLfixed param);
void glPointParameterxv(GLenum pname, const GLfixed *params);
void glPolygonOffsetx(GLfixed factor, GLfixed units);
void glRotatex(GLfixed angle, GLfixed x, GLfixed y, GLfixed z);
void glScalex(GLfixed x, GLfixed y, GLfixed z);
void glTexEnvx(GLenum target, GLenum pname, GLfixed param);
void glTexEnvxv(GLenum target, GLenum pname, const GLfixed *params);
void glMultiTexCoord4x(GLenum target, GLfixed s, GLfixed t, GLfixed r, GLfixed q);
void glGetTexEnvxv(GLenum target, GLenum pname, GLfixed *params);
void glTexParameterx(GLenum target, GLenum pname, GLfixed param);
void glTexParameterxv(GLenum target, GLenum pname, const GLfixed *params);
void glGetTexParameterxv(GLenum target, GLenum pname, GLfixed *params);
void glTranslatex(GLfixed x, GLfixed y, GLfixed z);
void glLightx(GLenum light, GLenum pname, GLfixed param);
void glLightxv(GLenum light, GLenum pname, const GLfixed *params);
void glLightModelx(GLenum pname, GLfixed param);
void glLightModelxv(GLenum pname, const GLfixed *params);
void glMaterialx(GLenum face, GLenum pname, GLfixed param);
void glMaterialxv(GLenum face, GLenum pname, const GLfixed *params);
void glGetLightxv(GLenum light, GLenum pname, GLfixed *params);
void glGetMaterialxv(GLenum face, GLenum pname, GLfixed *params);
void glFogx(GLenum pname, GLfixed param);
void glFogxv(GLenum pname, const GLfixed *params);
void glClipPlanex(GLenum plane, const GLfixed *equation);
void glGetClipPlanex(GLenum plane, GLfixed *equation);

#ifdef __cplusplus
}
#endif

#endif // C3DGL_GL_H

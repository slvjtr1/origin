/* Driver cost probes for the TV (SB_BENCHUNI=n): n small textured quads drawn the way a
   uniform-driven renderer does it (one static 4-vertex buffer, the rectangle and colour as
   uniforms, one draw per quad), to compare with the batched path. Dev only. */
#include "ui/gl.h"
#include "ui/gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GLuint prog, vbo, tex[64]; static GLint uRect, uView, uCol, aPos; static int ready, ntex;
static GLuint sh(GLenum t, const char *s) { const char *src[2] = { SB_GLSL_PRECISION, s }; GLuint o = glCreateShader(t); glShaderSource(o, 2, src, NULL); glCompileShader(o); return o; }
static void setup(void) {
  ready = 1;
  prog = glCreateProgram();
  glAttachShader(prog, sh(GL_VERTEX_SHADER, "attribute vec2 aPos; uniform vec4 uRect; uniform vec2 uView; varying vec2 vUv;\n"
    "void main(){ vUv = aPos; vec2 p = uRect.xy + aPos * uRect.zw; gl_Position = vec4(p.x / uView.x * 2.0 - 1.0, 1.0 - p.y / uView.y * 2.0, 0.0, 1.0); }\n"));
  glAttachShader(prog, sh(GL_FRAGMENT_SHADER, "uniform sampler2D uTex; uniform vec4 uCol; varying vec2 vUv; void main(){ gl_FragColor = texture2D(uTex, vUv) * uCol; }\n"));
  glBindAttribLocation(prog, 0, "aPos"); glLinkProgram(prog);
  uRect = glGetUniformLocation(prog, "uRect"); uView = glGetUniformLocation(prog, "uView"); uCol = glGetUniformLocation(prog, "uCol"); aPos = 0;
  static const GLfloat q[] = { 0, 0, 1, 0, 0, 1, 1, 1 };
  glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo); glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
  ntex = getenv("SB_BENCHUNI_TEX") ? atoi(getenv("SB_BENCHUNI_TEX")) : 1; if (ntex < 1) ntex = 1; if (ntex > 64) ntex = 64;
  unsigned char *px = malloc(64 * 64 * 4);
  for (int i = 0; i < ntex; i++) { memset(px, 60 + i * 3, 64 * 64 * 4); tex[i] = gfx_tex_create(px, 64, 64, 4, 1); }
  free(px);
}
void bench_uniform(int n, int surfW, int surfH) {
  if (n <= 0) return;
  gfx_flush();
  if (!ready) setup();
  glUseProgram(prog); glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
  for (int a = 1; a < 5; a++) glDisableVertexAttribArray(a);
  glUniform2f(uView, (float)surfW, (float)surfH);
  glActiveTexture(GL_TEXTURE0);
  float s = surfW / SB_W;
  for (int i = 0; i < n; i++) {
    glBindTexture(GL_TEXTURE_2D, tex[i % ntex]);
    glUniform4f(uRect, (40 + (i % 40) * 46) * s, (300 + (i / 40) * 46) * s, 40 * s, 40 * s);
    glUniform4f(uCol, 1, 1, 1, 1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  }
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  gfx_invalidate_state();
}

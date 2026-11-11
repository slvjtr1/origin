package app.stremioorigin;

import android.content.Context;
import android.opengl.GLES20;

import androidx.media3.common.VideoFrameProcessingException;
import androidx.media3.common.util.GlProgram;
import androidx.media3.common.util.GlUtil;
import androidx.media3.common.util.Size;
import androidx.media3.effect.BaseGlShaderProgram;
import androidx.media3.effect.GlEffect;
import androidx.media3.effect.GlShaderProgram;

/* Upscale (Settings › Playback): each decoded frame goes through the TV's graphics chip before it is shown. A picture
   below 1080 lines is drawn at 1080 lines; then every pixel is sharpened by how much contrast its neighbours leave room
   for (contrast-adaptive sharpening: edges and fine detail come up, flat areas and grain are left alone, nothing clips).
   Five reads per pixel at 1080p at most: light enough for a TV's chip. SDR pictures up to 1080p only (OriginVideo). */
final class OriginSharp implements GlEffect {
    private final float strength; /* 0..1 */
    OriginSharp(float strength) { this.strength = strength; }

    @Override public GlShaderProgram toGlShaderProgram(Context context, boolean useHdr) throws VideoFrameProcessingException { return new Program(useHdr, strength); }
    @Override public boolean isNoOp(int inputWidth, int inputHeight) { return false; }

    private static final String VS =
        "attribute vec4 aFramePosition;\n" +
        "uniform vec2 uPx;\n" +
        "varying vec2 vC; varying vec2 vN; varying vec2 vS; varying vec2 vW; varying vec2 vE;\n" +
        "void main() {\n" +
        "  gl_Position = aFramePosition;\n" +
        "  vC = aFramePosition.xy * 0.5 + 0.5;\n" +
        "  vN = vC + vec2(0.0, -uPx.y); vS = vC + vec2(0.0, uPx.y); vW = vC + vec2(-uPx.x, 0.0); vE = vC + vec2(uPx.x, 0.0);\n" +
        "}\n";
    private static final String FS =
        "precision mediump float;\n" +
        "uniform sampler2D uTex;\n" +
        "uniform float uPeak;\n" +
        "varying vec2 vC; varying vec2 vN; varying vec2 vS; varying vec2 vW; varying vec2 vE;\n" +
        "void main() {\n" +
        "  vec3 c = texture2D(uTex, vC).rgb, n = texture2D(uTex, vN).rgb, s = texture2D(uTex, vS).rgb, w = texture2D(uTex, vW).rgb, e = texture2D(uTex, vE).rgb;\n" +
        "  vec3 mn = min(c, min(min(n, s), min(w, e))), mx = max(c, max(max(n, s), max(w, e)));\n" +
        "  vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, vec3(0.004)), 0.0, 1.0));\n" +
        "  vec3 k = amp * uPeak;\n" +
        "  gl_FragColor = vec4(clamp((c + (n + s + w + e) * k) / (1.0 + 4.0 * k), 0.0, 1.0), 1.0);\n" +
        "}\n";

    private static final class Program extends BaseGlShaderProgram {
        private final GlProgram gl; private final float peak; private int inW = 1, inH = 1;
        Program(boolean useHdr, float strength) throws VideoFrameProcessingException {
            super(useHdr, 1);
            peak = -1f / (8f - 3f * Math.max(0f, Math.min(1f, strength))); /* -1/8 (mild) .. -1/5 (the strongest that never rings) */
            try { gl = new GlProgram(VS, FS); } catch (GlUtil.GlException e) { throw new VideoFrameProcessingException(e); }
            gl.setBufferAttribute("aFramePosition", GlUtil.getNormalizedCoordinateBounds(), GlUtil.HOMOGENEOUS_COORDINATE_VECTOR_SIZE);
        }
        @Override public Size configure(int inputWidth, int inputHeight) {
            inW = Math.max(1, inputWidth); inH = Math.max(1, inputHeight);
            if (inH >= 1000 || inW >= 1900) return new Size(inW, inH);
            float k = Math.min(1080f / inH, 1920f / inW); /* up to 1080 lines, never wider than 1920, its shape kept */
            return new Size(Math.max(2, Math.round(inW * k) & ~1), Math.max(2, Math.round(inH * k) & ~1));
        }
        @Override public void drawFrame(int inputTexId, long presentationTimeUs) throws VideoFrameProcessingException {
            try {
                gl.use();
                gl.setSamplerTexIdUniform("uTex", inputTexId, 0);
                gl.setFloatsUniform("uPx", new float[] { 1f / inW, 1f / inH });
                gl.setFloatUniform("uPeak", peak);
                gl.bindAttributesAndUniforms();
                GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);
            } catch (GlUtil.GlException e) { throw new VideoFrameProcessingException(e, presentationTimeUs); }
        }
        @Override public void release() throws VideoFrameProcessingException {
            super.release();
            try { gl.delete(); } catch (GlUtil.GlException e) { throw new VideoFrameProcessingException(e); }
        }
    }
}

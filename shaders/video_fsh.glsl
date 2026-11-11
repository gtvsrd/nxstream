#version 460
// Vídeo NV12 do NVDEC: luma (R8) e croma (RG8, metade do tamanho) -> RGB.
// Coeficientes YUV->RGB no uniforme (BT.709 ou BT.601, faixa limitada ou cheia).
layout (location = 0) in vec2 entradaUV;
layout (location = 0) out vec4 saidaCor;

layout (binding = 0) uniform sampler2D luma;
layout (binding = 1) uniform sampler2D croma;

layout (std140, binding = 0) uniform Cores {
    vec4 escala;  // x: multiplicador do Y, y: deslocamento do Y
    vec4 coefs;   // x: R<-V, y: G<-U, z: G<-V, w: B<-U
} cores;

void main() {
    float y = (texture(luma, entradaUV).r - cores.escala.y) * cores.escala.x;
    vec2 uv = texture(croma, entradaUV).rg - vec2(0.5);
    vec3 rgb = vec3(y + cores.coefs.x * uv.y,
                    y - cores.coefs.y * uv.x - cores.coefs.z * uv.y,
                    y + cores.coefs.w * uv.x);
    saidaCor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}

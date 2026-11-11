#version 460
// Retângulo que cobre a tela inteira, sem buffer de vértices: 4 vértices em
// TRIANGLE_STRIP. Com a origem no canto superior esquerdo (padrão do deko3d),
// y = -1 é o topo da tela e a coordenada de textura (0,0) é o canto superior esquerdo.
layout (location = 0) out vec2 saidaUV;

void main() {
    vec2 uv = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    saidaUV = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}

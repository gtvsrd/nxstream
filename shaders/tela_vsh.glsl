#version 460
// Retângulo que cobre a tela inteira, sem buffer de vértices: 4 vértices em
// TRIANGLE_STRIP. No deko3d (configuração padrão, como nos exemplos oficiais)
// y = +1 é o TOPO da tela, então a coordenada de textura v é invertida:
// o canto superior esquerdo da tela recebe (0,0) da imagem.
// (v0.7.0 usava y = -1 no topo e a imagem saiu invertida na vertical.)
layout (location = 0) out vec2 saidaUV;

void main() {
    vec2 uv = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    saidaUV = vec2(uv.x, 1.0 - uv.y);
}

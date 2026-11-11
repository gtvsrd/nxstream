#version 460
// Texto por cima do vídeo (estatísticas, menu): imagem RGBA com transparência
layout (location = 0) in vec2 entradaUV;
layout (location = 0) out vec4 saidaCor;

layout (binding = 2) uniform sampler2D sobre;

void main() {
    saidaCor = texture(sobre, entradaUV);
}

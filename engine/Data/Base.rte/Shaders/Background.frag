#version 330 core
#extension GL_KHR_blend_equation_advanced: enable
#extension GL_ARB_sample_shading: enable

in vec2 textureUV;
in vec4 vertexColor;

#ifdef GL_KHR_blend_equation_advanced
layout(blend_support_all_equations) out;
#endif
out vec4 FragColor;
uniform sampler2D rteTexture;
uniform sampler2D rtePalette;
uniform vec4 rteColor = vec4(1.0);
uniform bool rteBlendInvert = false;
uniform bool drawMasked = false;


// Palette entries are discrete colors, not a gradient. Sampling them through a
// filtered lookup lands on a texel boundary wherever the index is locally
// constant (zero derivative), which selects an arbitrary neighbouring entry and
// speckles flat areas with colors from an unrelated part of the palette. Fetch
// the exact entry instead so the result does not depend on the driver.
vec4 paletteEntry(float index) {
	return texelFetch(rtePalette, ivec2(int(clamp(index, 0.0, 1.0) * 255.0 + 0.5), 0), 0);
}

void main() {
	float red = texture(rteTexture, textureUV).r;
	if (red==0.0 && drawMasked) {
		discard;
	}
	if (!rteBlendInvert) {
		FragColor = paletteEntry(red * vertexColor.r) * vec4(rteColor.rgb, rteColor.a * vertexColor.a);
	} else {
		FragColor = vec4(vec3(1.0), 0.0) - (paletteEntry(red * vertexColor.r) * vec4(rteColor.rgb, -rteColor.a * vertexColor.a));
	}
}

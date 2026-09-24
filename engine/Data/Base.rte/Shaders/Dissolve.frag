#version 330 core

in vec2 textureUV;
in vec4 vertexColor;

out vec4 FragColor;

uniform sampler2D rteTexture;
uniform sampler2D rtePalette;
uniform vec4 rteColor;

// Pseudo random number generator. 
float hash( vec2 a )
{
    return fract( sin( a.x * 3433.8 + a.y * 3843.98 ) * 45933.8 );
}

// Value noise courtesy of BigWingz 
// check his youtube channel he has
// a video of this one.
// Succint version by FabriceNeyret
float noise( vec2 U )
{
	vec2 id = floor( U );
	U = fract( U );
	U *= U * ( 3. - 2. * U );  

	vec2 A = vec2( hash(id), hash(id + vec2(0,1)) );
	vec2 B = vec2( hash(id + vec2(1,0)), hash(id + vec2(1,1)) );
	vec2 C = mix( A, B, U.x);

	return mix( C.x, C.y, U.y );
}

// Palette entries are discrete colors, not a gradient. Sampling them through a
// filtered lookup lands on a texel boundary wherever the index is locally
// constant (zero derivative), which selects an arbitrary neighbouring entry and
// speckles flat areas with colors from an unrelated part of the palette. Fetch
// the exact entry instead so the result does not depend on the driver.
vec4 paletteEntry(float index) {
	return texelFetch(rtePalette, ivec2(int(clamp(index, 0.0, 1.0) * 255.0 + 0.5), 0), 0);
}

void main() {
	if (noise(gl_FragCoord.xy + textureUV) < 0.5) {
		discard;
	}
	float red = texture(rteTexture, textureUV).r;
	vec4 color = paletteEntry(red * vertexColor.r) * vec4(rteColor.rgb, rteColor.a * vertexColor.a);

	FragColor = color;
}
#version 430 core
out vec4 FragColor;
uniform sampler2D uAccumTex;
uniform float     uExposure; // e.g. 1.6–2.2

// XYZ (D65) -> linear sRGB
// NOTE: GLSL matrices are column-major. Supply columns, not rows.
const mat3 XYZ2sRGB = mat3(
    3.2406,  -0.9689,  0.0557,   // column 0
   -1.5372,   1.8758, -0.2040,   // column 1
   -0.4986,   0.0415,  1.0570    // column 2
);

vec3 tonemapACES(vec3 x){
    const float a=2.51, b=0.03, c=2.43, d=0.59, e=0.14;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

void main(){
    vec2 uv  = gl_FragCoord.xy / vec2(textureSize(uAccumTex, 0));
    vec3 xyz = texture(uAccumTex, uv).rgb;

    // Convert XYZ->linear sRGB (D65)
    vec3 rgb = XYZ2sRGB * xyz;

    // Optional exposure
    rgb *= uExposure;

    // Tonemap + gamma
    vec3 ldr = tonemapACES(max(rgb, vec3(0.0)));
    FragColor = vec4(pow(ldr, vec3(1.0/2.2)), 1.0);
}
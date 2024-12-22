#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require


layout(location = 0) out vec4 out_fragColor;

layout(location = 0) in VS_OUT
{
  vec2 texCoord;
} surf;

layout(binding = 0) uniform sampler2D image;

layout(binding = 1) readonly buffer d_t
{
  float density[1024];
} hist;

void main(void)
{
  vec4 color = texture(image, surf.texCoord);
  float brightness = clamp(max(color.r, max(color.g, color.b)), 0.0, 1.0);
  float dense = hist.density[int(1023. * brightness)];
  out_fragColor = vec4(color.rgb * exp(dense), 1.0);
}

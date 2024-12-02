#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0 ) out VS_OUT
{
  vec2 texCoord;
} vOut;

void main(void)
{
  gl_Position = vec4(-1.0 + 4.0 * float(gl_VertexIndex == 1), -1.0 + 4.0 * float(gl_VertexIndex == 2), 0.0, 1.0);
  vOut.texCoord = 0.5 + gl_Position.xy / 2.0;
}

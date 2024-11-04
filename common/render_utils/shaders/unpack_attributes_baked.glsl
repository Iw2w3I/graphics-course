#ifndef UNPACK_ATTRIBUTES_BAKED_GLSL_INCLUDED
#define UNPACK_ATTRIBUTES_BAKED_GLSL_INCLUDED

// NOTE: .glsl extension is used for helper files with shader code

vec3 decode_normal(uint a_data)
{
  const uint a_enc_x = (a_data & 0x000000ff);
  const uint a_enc_y = ((a_data & 0x0000ff00) >> 8);
  const uint a_enc_z = ((a_data & 0x00ff0000) >> 16);

  ivec3 int_enc = ivec3(a_enc_x, a_enc_y, a_enc_z);
  int_enc = ((int_enc + 128) % 256) - 128;
  vec3 true_enc = vec3(int_enc);

  return max(true_enc / 127.0, -1.0);
}

#endif // UNPACK_ATTRIBUTES_BAKED_GLSL_INCLUDED
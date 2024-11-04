#ifndef UNPACK_ATTRIBUTES_BAKED_GLSL_INCLUDED
#define UNPACK_ATTRIBUTES_BAKED_GLSL_INCLUDED

// NOTE: .glsl extension is used for helper files with shader code

vec3 decode_normal(uint normal)
{
  const uint x = (normal & 0x000000ff);
  const uint y = ((normal & 0x0000ff00) >> 8);
  const uint z = ((normal & 0x00ff0000) >> 16);

  ivec3 output = ivec3(x, y, z);
  output = ((output + 128) % 256) - 128;
  return max(-1.0, vec3(output) / 127.0);
}

#endif // UNPACK_ATTRIBUTES_BAKED_GLSL_INCLUDED
#ifndef UNPACK_ATTRIBUTES_BAKED_GLSL_INCLUDED
#define UNPACK_ATTRIBUTES_BAKED_GLSL_INCLUDED

// NOTE: .glsl extension is used for helper files with shader code

vec3 decode_normal(uint normal)
{
  uint x = (normal & 0x000000ff);
  uint y = ((normal & 0x0000ff00) >> 8);
  uint z = ((normal & 0x00ff0000) >> 16);

  ivec3 transformed = ivec3(x, y, z);
  transformed = ((transformed + 128) % 256) - 128;
  return max(vec3(-1.0), vec3(transformed) / 127.0);
}

#endif // UNPACK_ATTRIBUTES_BAKED_GLSL_INCLUDED
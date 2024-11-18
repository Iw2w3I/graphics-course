#version 430
#extension GL_GOOGLE_include_directive : require
#include "cpp_glsl_compat.h"


layout(binding = 0) uniform sampler2D iChannel0;
layout(binding = 1) uniform sampler2D texture_image;

layout(location = 0) out vec4 out_fragColor;

layout(push_constant) uniform Params {
  // vec2 iResolution;
  float iResolution_x;
  float iResolution_y;
  // vec2 iMouse;
  float iMouse_x;
  float iMouse_y;
  float iTime;
} params;

#define MAX_STEPS 2000
#define MAX_DIST 300.0
#define EPS 0.00001
#define TRUST 0.1

mat3 rotateX(float theta) {
    float c = cos(theta);
    float s = sin(theta);
    return mat3(
        vec3(1, 0, 0),
        vec3(0, c, -s),
        vec3(0, s, c)
    );
}

mat3 rotateY(float theta) {
    float c = cos(theta);
    float s = sin(theta);
    return mat3(
        vec3(c, 0, s),
        vec3(0, 1, 0),
        vec3(-s, 0, c)
    );
}

mat3 rotateZ(float theta) {
    float c = cos(theta);
    float s = sin(theta);
    return mat3(
        vec3(c, -s, 0),
        vec3(s, c, 0),
        vec3(0, 0, 1)
    );
}

float dFace(vec3 p, vec3 center, float size) {
    float dist = 0.0;
    dist += pow(abs(p.x - center.x), 6.0);
    dist += pow(abs(p.y - center.y), 2.5);
    dist += pow(abs(p.z - center.z), 2.0);
    dist = pow(dist, 0.5);
    return dist - size;
}

float dTor(vec3 p, vec3 center, vec2 size, mat3 rotation) {
    vec3 shift = (p - center) * rotation;
    vec2 q = vec2(length(shift.xz) - size.x, shift.y);
    return length(q) - size.y;
}

float dEye(vec3 p, vec3 center, float size, float spikes, float sharpness) {
    float x = (p - center).x;
    float y = (p - center).y;
    float z = (p - center).z;
    float alpha = asin(abs(x) / sqrt(x * x + z * z));
    float beta = asin(abs(y) / sqrt(x * x + y * y));
    float gamma = asin(abs(z) / sqrt(y * y + z * z));
    float spikeA = pow(sin(alpha * spikes), sharpness);
    float spikeB = pow(sin(beta * spikes), sharpness);
    float spikeC = pow(sin(gamma * spikes), sharpness);
    return length(p - center) - size - min(spikeA, min(spikeB, spikeC));
}

float sdf(vec3 p, mat3 rotate) {
    p *= rotate;
    mat3 spin = rotateX(params.iTime);
    vec3 dEye1pos = vec3(0, 0, -2);
    vec3 dEye2pos = vec3(0, 0, 2);
    
    float outSdf = dEye((p - dEye1pos) * spin + dEye1pos, dEye1pos, 0.5, 1.0 + pow(params.iTime, 2.0) * 0.01, 5.0);
    outSdf = min(outSdf, dEye((p - dEye2pos) * spin + dEye2pos, dEye2pos, 0.5, 1.0 + pow(params.iTime, 2.0) * 0.01, 5.0));
    outSdf = min(outSdf, dFace(p, vec3(1.5, -1, 0), 5.0));
    outSdf = min(outSdf, dTor(p, vec3(0.5, -1.5, -1.5), vec2(1.5, 0.5), rotateX(0.5) * rotateZ(-1.0)));
    outSdf = min(outSdf, dTor(p, vec3(0.5, -1.5, 1.5), vec2(1.5, 0.5), rotateX(-0.5) * rotateZ(-1.0)));
    return outSdf;
}

vec3 trace (in vec3 from, in vec3 dir, out bool hit, in mat3 rotate) {
    vec3 p = from;
    float totalDist = 0.0;
    hit = false;
    for (int steps = 0; steps < MAX_STEPS; ++steps) {
        float dist = sdf(p, rotate);
        if (dist < EPS) {
            hit = true;
            break;
        }
        totalDist += dist;
        if (totalDist > MAX_DIST)
            break;
        p += dist * dir * TRUST;
    }
    return p;
}

vec3 normal(vec3 z, float d, mat3 rotate) {
    float step = max(d * 0.5, EPS);
    float dx1 = sdf(z + vec3(step, 0, 0), rotate);
    float dx2 = sdf(z - vec3(step, 0, 0), rotate);
    float dy1 = sdf(z + vec3(0, step, 0), rotate);
    float dy2 = sdf(z - vec3(0, step, 0), rotate);
    float dz1 = sdf(z + vec3(0, 0, step), rotate);
    float dz2 = sdf(z - vec3(0, 0, step), rotate);
    return normalize(vec3 (dx1 - dx2, dy1 - dy2, dz1 - dz2));

}

const vec3 eye = vec3(0, 0, 10);
const vec3 light = vec3(0.0, 1.0, 15.0);
const float brightness = 3.0;

void main()
{
  vec2 iResolution = vec2(params.iResolution_x, params.iResolution_y);
  vec2 iMouse = vec2(params.iMouse_x, params.iMouse_y);

  bool hit;
  vec3 mouse = vec3(iMouse.xy/iResolution.xy - 0.5, 0.0);
  mat3 rotate = rotateX(mouse.y * 6.0) * rotateY(-mouse.x * 6.0 + 1.5) * rotateZ(acos(-1.0));
  vec2 uv = (gl_FragCoord.xy/iResolution.xy - vec2(0.5)) * 20.0 * iResolution.xy / max(iResolution.x, iResolution.y);
  vec3 wDir = normalize(vec3(uv, 0) - eye);
  vec3 wP = trace(eye, wDir, hit, rotate);
  vec3 cP = wP * rotate;
  vec3 color = vec3(0.5, asin(cos(uv * acos(-1.0) * 0.4))) * 2.0 * rotateX(params.iTime);
  if (hit) {
      vec3 l = normalize(light - wP);
      vec3 v = normalize(eye - wP);
      vec3 n = normal(wP, EPS, rotate);
      float nl = max(0.0, dot(n, l));
      vec3 h = normalize(l + v);
      float nh = max(0.0, dot(n, h));
      float blick = pow(nh, 100.0);
      
      vec3 texNorm = abs(n);
      texNorm /= (texNorm.x + texNorm.y + texNorm.z);
      vec3 tex = texture(texture_image, cP.xy).rgb * texNorm.z;
      tex *= brightness;
      if (cP.x < 0.0) {
        tex = vec3(1.5);
      }
      
      color = tex * (nl + blick) / 2.0;
  }
  out_fragColor = vec4(color, 1.0);
}

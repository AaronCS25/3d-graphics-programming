#version 330 core
// Pass 2. One attribute per OBJ list (task 12), the tangent for normal maps
// (task 11) and the normalized geodesic distance of the vertex (task 06).
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec4 aTangent;
layout (location = 4) in float aDistance;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;
uniform mat4 uLightSpace;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoords;
out vec4 vTangent;
out vec4 vLightSpacePos;
out float vDistance;
out float vViewDepth;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = uNormalMatrix * aNormal;
    vTexCoords = aTexCoords;
    vTangent = vec4(mat3(uModel) * aTangent.xyz, aTangent.w);
    vLightSpacePos = uLightSpace * world;
    vDistance = aDistance;
    vec4 eye = uView * world;
    vViewDepth = -eye.z;
    gl_Position = uProjection * eye;
}

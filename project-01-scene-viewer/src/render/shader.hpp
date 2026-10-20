// A GLSL program read from a vertex and a fragment file, with its uniform
// locations looked up once and cached by name.

#pragma once

#include <glad/glad.h>

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>

class Shader {
  public:
    // Reads, compiles and links the two files. Prints the compiler log and
    // returns false on failure.
    bool load(const std::string& vertex_path, const std::string& fragment_path);
    void destroy();

    void use() const {
        glUseProgram(m_id);
    }

    void set(const char* name, int value);
    void set(const char* name, bool value);
    void set(const char* name, float value);
    void set(const char* name, const glm::vec3& value);
    void set(const char* name, const glm::mat3& value);
    void set(const char* name, const glm::mat4& value);

  private:
    GLint location(const char* name);

    GLuint m_id = 0;
    std::unordered_map<std::string, GLint> m_locations;
};

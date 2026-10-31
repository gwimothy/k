#pragma once

#include "shared/kmath.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace k {

class Shader {
public:
    Shader() = default;
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& o) noexcept : program_(o.program_) { o.program_ = 0; }
    Shader& operator=(Shader&& o) noexcept;
    ~Shader();

    // Loads "<name>.vert" and "<name>.frag" from shaders/ through the filesystem.
    bool load(std::string_view name);
    bool compile(std::string_view vertexSource, std::string_view fragmentSource, std::string_view debugName);

    void bind() const;
    void set(const char* name, float v) const;
    void set(const char* name, const Vec3& v) const;
    void set(const char* name, const Vec4& v) const;
    void set(const char* name, const Mat4& m) const;
    void set(const char* name, int v) const;

    bool valid() const { return program_ != 0; }
    uint32_t id() const { return program_; }

private:
    uint32_t program_ = 0;
};

} // namespace k

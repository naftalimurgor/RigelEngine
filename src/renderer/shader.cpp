/* Copyright (C) 2016, Nikolai Wuttke. All rights reserved.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "shader.hpp"

#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>


namespace rigel::renderer
{

namespace
{

#ifdef RIGEL_USE_GL_ES

const auto SHADER_PREAMBLE = R"shd(
#version 100

#define ATTRIBUTE attribute
#define OUT varying
#define IN varying
#define TEXTURE_LOOKUP texture2D
#define OUTPUT_COLOR gl_FragColor
#define OUTPUT_COLOR_DECLARATION
#define SET_POINT_SIZE(size) gl_PointSize = size;
#define HIGHP highp

precision mediump float;
)shd";

#else

  // We generally want to stick to GLSL version 130 (from OpenGL 3.0) in order
  // to maximize compatibility with older graphics cards. Unfortunately, Mac OS
  // only supports GLSL 150 (from OpenGL 3.2), even when requesting a OpenGL 3.0
  // context. Therefore, we use different GLSL versions depending on the
  // platform.
  #if defined(__APPLE__)
const auto SHADER_PREAMBLE = R"shd(
#version 150

#define ATTRIBUTE in
#define OUT out
#define IN in
#define TEXTURE_LOOKUP texture
#define OUTPUT_COLOR outputColor
#define OUTPUT_COLOR_DECLARATION out vec4 outputColor;
#define SET_POINT_SIZE
#define HIGHP
)shd";
  #else
const auto SHADER_PREAMBLE = R"shd(
#version 130

#define ATTRIBUTE in
#define OUT out
#define IN in
#define TEXTURE_LOOKUP texture2D
#define OUTPUT_COLOR outputColor
#define OUTPUT_COLOR_DECLARATION out vec4 outputColor;
#define SET_POINT_SIZE
#define HIGHP
)shd";
  #endif

#endif


GlHandleWrapper compileShader(const std::string& source, GLenum type)
{
  auto shader = GlHandleWrapper{glCreateShader(type), glDeleteShader};
  const auto sourcePtr = source.c_str();
  glShaderSource(shader.mHandle, 1, &sourcePtr, nullptr);
  glCompileShader(shader.mHandle);

  GLint compileStatus = 0;
  glGetShaderiv(shader.mHandle, GL_COMPILE_STATUS, &compileStatus);

  if (!compileStatus)
  {
    GLint infoLogSize = 0;
    glGetShaderiv(shader.mHandle, GL_INFO_LOG_LENGTH, &infoLogSize);

    if (infoLogSize > 0)
    {
      std::unique_ptr<char[]> infoLogBuffer(new char[infoLogSize]);
      glGetShaderInfoLog(
        shader.mHandle, infoLogSize, nullptr, infoLogBuffer.get());

      throw std::runtime_error(
        std::string{"Shader compilation failed:\n\n"} + infoLogBuffer.get());
    }
    else
    {
      throw std::runtime_error(
        "Shader compilation failed, but could not get info log");
    }
  }

  return shader;
}


void* toAttribOffset(std::uintptr_t offset)
{
  return reinterpret_cast<void*>(offset);
}

} // namespace


Shader::Shader(const ShaderSpec& spec)
  : mProgram(glCreateProgram(), glDeleteProgram)
  , mAttributeDescs(spec.mAttributeDescs)
{
  auto vertexShader = compileShader(
    std::string{SHADER_PREAMBLE} + spec.mVertexSource, GL_VERTEX_SHADER);
  auto fragmentShader = compileShader(
    std::string{SHADER_PREAMBLE} + spec.mFragmentSource, GL_FRAGMENT_SHADER);

  glAttachShader(mProgram.mHandle, vertexShader.mHandle);
  glAttachShader(mProgram.mHandle, fragmentShader.mHandle);

  for (GLuint index = 0; index < spec.mAttributeDescs.size(); ++index)
  {
    glBindAttribLocation(
      mProgram.mHandle, index, spec.mAttributeDescs[index].mName);
  }

  glLinkProgram(mProgram.mHandle);

  GLint linkStatus = 0;
  glGetProgramiv(mProgram.mHandle, GL_LINK_STATUS, &linkStatus);
  if (!linkStatus)
  {
    GLint infoLogSize = 0;
    glGetProgramiv(mProgram.mHandle, GL_INFO_LOG_LENGTH, &infoLogSize);

    if (infoLogSize > 0)
    {
      std::unique_ptr<char[]> infoLogBuffer(new char[infoLogSize]);
      glGetProgramInfoLog(
        mProgram.mHandle, infoLogSize, nullptr, infoLogBuffer.get());

      throw std::runtime_error(
        std::string{"Shader program linking failed:\n\n"} +
        infoLogBuffer.get());
    }
    else
    {
      throw std::runtime_error(
        "Shader program linking failed, but could not get info log");
    }
  }
}


void Shader::use()
{
  using std::begin;
  using std::end;

  glUseProgram(mProgram.mHandle);

  const auto totalValueCount = std::accumulate(
    begin(mAttributeDescs),
    end(mAttributeDescs),
    0,
    [](const int count, const AttributeDescription& desc) {
      return count + desc.mValueCount;
    });

  const auto stride = GLsizei(sizeof(float) * totalValueCount);

  std::uintptr_t nextOffset = 0;
  for (GLuint index = 0; index < mAttributeDescs.size(); ++index)
  {
    const auto valueCount = mAttributeDescs[index].mValueCount;

    glVertexAttribPointer(
      index,
      valueCount,
      GL_FLOAT,
      GL_FALSE,
      stride,
      toAttribOffset(nextOffset));

    nextOffset += valueCount * sizeof(float);
  }
}


GLint Shader::location(const std::string& name) const
{
  auto it = mLocationCache.find(name);
  if (it == mLocationCache.end())
  {
    const auto location = glGetUniformLocation(mProgram.mHandle, name.c_str());
    std::tie(it, std::ignore) = mLocationCache.emplace(name, location);
  }

  return it->second;
}

} // namespace rigel::renderer

#pragma once

#include <render/IRenderable.h>
#include <render/IGraphicsAPI.h>
#include <service/IService.h>
#include <math/Vec.h>
#include <math/Mat4.h>
#include <GL/glew.h>
#include <vector>
#include <string>
#include <stdexcept>
#include <cmath>

class Maze;

//=============================================================================
// CameraState
//
// Service holding the first-person camera orientation and the projection
// matrix. PlayerSystem writes position and angles each frame;
// MazeRenderer reads them to build the view-projection matrix.
//=============================================================================
struct CameraState : public IService
{
	Vec3 Position;
	float Yaw = 0.0f;
	float Pitch = 0.0f;
	Mat4f Projection;

	Mat4f GetViewMatrix() const
	{
		float cy = std::cos(Yaw);
		float sy = std::sin(Yaw);
		float cp = std::cos(Pitch);
		float sp = std::sin(Pitch);

		Vec3 forward(sy * cp, sp, -cy * cp);
		Vec3 target = Position + forward;
		Vec3 up(0.0f, 1.0f, 0.0f);

		return Mat4f::LookAt(Position, target, up);
	}
};

//=============================================================================
// MazeRenderer
//
// IRenderable that builds and draws the maze geometry using OpenGL.
// Constructs a static mesh from the Maze grid on initialization:
// wall blocks, floor, and ceiling are emitted as colored quads.
//
// Each frame, it computes the MVP matrix from the CameraState and
// uploads it as a uniform before issuing the draw call.
//=============================================================================
class MazeRenderer : public IRenderable
{
public:
	MazeRenderer(const Maze& maze, CameraState& camera)
		: Camera(camera)
	{
		BuildShader();
		BuildMesh(maze);
	}

	~MazeRenderer()
	{
		if (VAO) glDeleteVertexArrays(1, &VAO);
		if (VBO) glDeleteBuffers(1, &VBO);
		if (ShaderProgram) glDeleteProgram(ShaderProgram);
	}

	MazeRenderer(const MazeRenderer&) = delete;
	MazeRenderer& operator=(const MazeRenderer&) = delete;

	void Render(IGraphicsAPI&) override
	{
		glUseProgram(ShaderProgram);

		Mat4f vp = Camera.Projection * Camera.GetViewMatrix();
		glUniformMatrix4fv(MVPLocation, 1, GL_FALSE, vp.GetData());

		glBindVertexArray(VAO);
		glDrawArrays(GL_TRIANGLES, 0, VertexCount);
		glBindVertexArray(0);
		glUseProgram(0);
	}

	int GetRenderOrder() const override { return 0; }

private:
	struct Vertex
	{
		Vec3 Position;
		Vec3 Normal;
		Vec3 Color;
	};

	// -- Shader compilation -------------------------------------------------

	void BuildShader();

	static GLuint CompileShader(GLenum type, const char* source)
	{
		GLuint shader = glCreateShader(type);
		glShaderSource(shader, 1, &source, nullptr);
		glCompileShader(shader);

		GLint success;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
		if (!success)
		{
			char log[512];
			glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
			glDeleteShader(shader);
			throw std::runtime_error(std::string("Shader compile error: ") + log);
		}
		return shader;
	}

	static GLuint LinkProgram(GLuint vert, GLuint frag)
	{
		GLuint program = glCreateProgram();
		glAttachShader(program, vert);
		glAttachShader(program, frag);
		glLinkProgram(program);

		GLint success;
		glGetProgramiv(program, GL_LINK_STATUS, &success);
		if (!success)
		{
			char log[512];
			glGetProgramInfoLog(program, sizeof(log), nullptr, log);
			glDeleteProgram(program);
			throw std::runtime_error(std::string("Shader link error: ") + log);
		}

		glDeleteShader(vert);
		glDeleteShader(frag);
		return program;
	}

	// -- Mesh construction --------------------------------------------------

	void BuildMesh(const Maze& maze);

	void EmitQuad(std::vector<Vertex>& verts,
	              Vec3 a, Vec3 b, Vec3 c, Vec3 d,
	              Vec3 normal, Vec3 color)
	{
		verts.push_back({a, normal, color});
		verts.push_back({b, normal, color});
		verts.push_back({c, normal, color});
		verts.push_back({a, normal, color});
		verts.push_back({c, normal, color});
		verts.push_back({d, normal, color});
	}

	// -- GL handles ---------------------------------------------------------

	GLuint VAO = 0;
	GLuint VBO = 0;
	GLuint ShaderProgram = 0;
	GLint MVPLocation = -1;
	GLsizei VertexCount = 0;

	CameraState& Camera;
};

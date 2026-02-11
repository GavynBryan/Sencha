#pragma once

#include <teapot/render/IGraphicsAPI.h>
#include <cstdint>

//=============================================================================
// GLEWGraphicsAPI
//
// Concrete IGraphicsAPI implementation using GLEW for OpenGL function
// loading and core-profile OpenGL for rendering. This is the first
// Infuser-layer backend.
//
// GLEW handles extension loading only — it does not create windows or GL
// contexts. The caller is responsible for creating a GL context (via
// GLFW, SDL, etc.) before calling Initialize(). Present() delegates to a
// user-supplied callback since buffer swapping is windowing-system work.
//
// Usage:
//   // After creating a GL context with your windowing library:
//   GLEWGraphicsAPI api;
//   api.SetPresentCallback([]{ glfwSwapBuffers(window); });
//   api.Initialize();
//
//   // Then register as a RenderContext:
//   contextService.AddContext(&api);
//=============================================================================
class GLEWGraphicsAPI : public IGraphicsAPI
{
public:
	using PresentCallback = void(*)();

	GLEWGraphicsAPI();
	~GLEWGraphicsAPI() override;

	// Call after a valid GL context exists. Returns false on failure.
	bool Initialize();

	void SetPresentCallback(PresentCallback callback);
	void SetViewport(int x, int y, int width, int height);

	// -- IGraphicsAPI -------------------------------------------------------

	bool IsValid() const override;
	void BeginFrame() override;
	void EndFrame() override;
	void Clear() override;
	void Present() override;

	void SetClearColor(float r, float g, float b, float a) override;
	void Submit2D(const Transform2D& transform) override;
	void Submit3D(const Transform3D& transform) override;

private:
	void SetupShaders();
	void SetupQuadGeometry();
	void SetupCubeGeometry();
	void CleanupGL();

	static uint32_t CompileShader(uint32_t type, const char* source);
	static uint32_t LinkProgram(uint32_t vertexShader, uint32_t fragmentShader);

	bool bInitialized = false;
	PresentCallback OnPresent = nullptr;

	float ClearR = 0.0f;
	float ClearG = 0.0f;
	float ClearB = 0.0f;
	float ClearA = 1.0f;

	// GL resource handles (GLuint stored as uint32_t to avoid GL header in this header)
	uint32_t ShaderProgram2D = 0;
	uint32_t Uniform2D_Position = 0;
	uint32_t Uniform2D_Scale = 0;
	uint32_t Uniform2D_Rotation = 0;
	uint32_t QuadVAO = 0;
	uint32_t QuadVBO = 0;

	uint32_t ShaderProgram3D = 0;
	uint32_t Uniform3D_Position = 0;
	uint32_t Uniform3D_Scale = 0;
	uint32_t Uniform3D_Rotation = 0;
	uint32_t CubeVAO = 0;
	uint32_t CubeVBO = 0;
};

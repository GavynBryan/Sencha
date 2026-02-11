#include <infuser/render/GLEWGraphicsAPI.h>
#include <GL/glew.h>
#include <cmath>
#include <cstring>

//=============================================================================
// Shader sources
//=============================================================================

static const char* QuadVertexShaderSource = R"glsl(
#version 330 core
layout (location = 0) in vec2 aPos;

uniform vec2 uPosition;
uniform vec2 uScale;
uniform float uRotation;

void main()
{
    float c = cos(uRotation);
    float s = sin(uRotation);
    vec2 rotated = vec2(
        aPos.x * c - aPos.y * s,
        aPos.x * s + aPos.y * c
    );
    vec2 scaled = rotated * uScale;
    vec2 final = scaled + uPosition;
    gl_Position = vec4(final, 0.0, 1.0);
}
)glsl";

static const char* QuadFragmentShaderSource = R"glsl(
#version 330 core
out vec4 FragColor;

void main()
{
    FragColor = vec4(1.0, 1.0, 1.0, 1.0);
}
)glsl";

static const char* CubeVertexShaderSource = R"glsl(
#version 330 core
layout (location = 0) in vec3 aPos;

uniform vec3 uPosition;
uniform vec3 uScale;
uniform vec3 uRotation;

void main()
{
    // Euler rotation: Rz * Ry * Rx
    float cx = cos(uRotation.x); float sx = sin(uRotation.x);
    float cy = cos(uRotation.y); float sy = sin(uRotation.y);
    float cz = cos(uRotation.z); float sz = sin(uRotation.z);

    vec3 p = aPos * uScale;

    // Rotate around X
    float y1 = p.y * cx - p.z * sx;
    float z1 = p.y * sx + p.z * cx;
    p.y = y1; p.z = z1;

    // Rotate around Y
    float x2 = p.x * cy + p.z * sy;
    float z2 = -p.x * sy + p.z * cy;
    p.x = x2; p.z = z2;

    // Rotate around Z
    float x3 = p.x * cz - p.y * sz;
    float y3 = p.x * sz + p.y * cz;
    p.x = x3; p.y = y3;

    vec3 final = p + uPosition;

    // Simple perspective: divide by -z for depth
    float w = 1.0 - final.z * 0.5;
    gl_Position = vec4(final.xy, final.z * 0.5 + 0.5, max(w, 0.001));
}
)glsl";

static const char* CubeFragmentShaderSource = R"glsl(
#version 330 core
out vec4 FragColor;

void main()
{
    FragColor = vec4(1.0, 1.0, 1.0, 1.0);
}
)glsl";

//=============================================================================
// Unit quad vertices (centered at origin, 1x1)
//=============================================================================
static const float QuadVertices[] = {
	-0.5f, -0.5f,
	 0.5f, -0.5f,
	 0.5f,  0.5f,
	-0.5f, -0.5f,
	 0.5f,  0.5f,
	-0.5f,  0.5f,
};

//=============================================================================
// Unit cube vertices (centered at origin, 1x1x1, 36 vertices for 12 tris)
//=============================================================================
static const float CubeVertices[] = {
	// Front face
	-0.5f, -0.5f,  0.5f,   0.5f, -0.5f,  0.5f,   0.5f,  0.5f,  0.5f,
	-0.5f, -0.5f,  0.5f,   0.5f,  0.5f,  0.5f,  -0.5f,  0.5f,  0.5f,
	// Back face
	 0.5f, -0.5f, -0.5f,  -0.5f, -0.5f, -0.5f,  -0.5f,  0.5f, -0.5f,
	 0.5f, -0.5f, -0.5f,  -0.5f,  0.5f, -0.5f,   0.5f,  0.5f, -0.5f,
	// Left face
	-0.5f, -0.5f, -0.5f,  -0.5f, -0.5f,  0.5f,  -0.5f,  0.5f,  0.5f,
	-0.5f, -0.5f, -0.5f,  -0.5f,  0.5f,  0.5f,  -0.5f,  0.5f, -0.5f,
	// Right face
	 0.5f, -0.5f,  0.5f,   0.5f, -0.5f, -0.5f,   0.5f,  0.5f, -0.5f,
	 0.5f, -0.5f,  0.5f,   0.5f,  0.5f, -0.5f,   0.5f,  0.5f,  0.5f,
	// Top face
	-0.5f,  0.5f,  0.5f,   0.5f,  0.5f,  0.5f,   0.5f,  0.5f, -0.5f,
	-0.5f,  0.5f,  0.5f,   0.5f,  0.5f, -0.5f,  -0.5f,  0.5f, -0.5f,
	// Bottom face
	-0.5f, -0.5f, -0.5f,   0.5f, -0.5f, -0.5f,   0.5f, -0.5f,  0.5f,
	-0.5f, -0.5f, -0.5f,   0.5f, -0.5f,  0.5f,  -0.5f, -0.5f,  0.5f,
};

//=============================================================================
// Construction / Destruction
//=============================================================================

GLEWGraphicsAPI::GLEWGraphicsAPI() = default;

GLEWGraphicsAPI::~GLEWGraphicsAPI()
{
	CleanupGL();
}

//=============================================================================
// Initialization
//=============================================================================

bool GLEWGraphicsAPI::Initialize()
{
	glewExperimental = GL_TRUE;
	GLenum err = glewInit();
	if (err != GLEW_OK)
	{
		bInitialized = false;
		return false;
	}

	// Consume any spurious error from glewInit (known GLEW quirk)
	glGetError();

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	SetupShaders();
	SetupQuadGeometry();
	SetupCubeGeometry();

	bInitialized = true;
	return true;
}

//=============================================================================
// Configuration
//=============================================================================

void GLEWGraphicsAPI::SetPresentCallback(PresentCallback callback)
{
	OnPresent = callback;
}

void GLEWGraphicsAPI::SetViewport(int x, int y, int width, int height)
{
	if (bInitialized)
	{
		glViewport(x, y, width, height);
	}
}

void GLEWGraphicsAPI::SetClearColor(float r, float g, float b, float a)
{
	ClearR = r;
	ClearG = g;
	ClearB = b;
	ClearA = a;
}

//=============================================================================
// Frame lifecycle
//=============================================================================

bool GLEWGraphicsAPI::IsValid() const
{
	return bInitialized;
}

void GLEWGraphicsAPI::BeginFrame()
{
	// Frame-start state can be set up here as the engine grows
}

void GLEWGraphicsAPI::Clear()
{
	glClearColor(ClearR, ClearG, ClearB, ClearA);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GLEWGraphicsAPI::EndFrame()
{
	glFlush();
}

void GLEWGraphicsAPI::Present()
{
	if (OnPresent)
	{
		OnPresent();
	}
}

//=============================================================================
// 2D submission
//=============================================================================

void GLEWGraphicsAPI::Submit2D(const Vec2& position, const Vec2& scale, float rotation)
{
	glUseProgram(ShaderProgram2D);
	glUniform2f(Uniform2D_Position, position.X(), position.Y());
	glUniform2f(Uniform2D_Scale, scale.X(), scale.Y());
	glUniform1f(Uniform2D_Rotation, rotation);

	glBindVertexArray(QuadVAO);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
}

//=============================================================================
// 3D submission
//=============================================================================

void GLEWGraphicsAPI::Submit3D(const Vec3& position, const Vec3& scale, const Vec3& rotation)
{
	glUseProgram(ShaderProgram3D);
	glUniform3f(Uniform3D_Position, position.X(), position.Y(), position.Z());
	glUniform3f(Uniform3D_Scale, scale.X(), scale.Y(), scale.Z());
	glUniform3f(Uniform3D_Rotation, rotation.X(), rotation.Y(), rotation.Z());

	glBindVertexArray(CubeVAO);
	glDrawArrays(GL_TRIANGLES, 0, 36);
	glBindVertexArray(0);
}

//=============================================================================
// Shader compilation helpers
//=============================================================================

uint32_t GLEWGraphicsAPI::CompileShader(uint32_t type, const char* source)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);

	GLint success = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glDeleteShader(shader);
		return 0;
	}

	return shader;
}

uint32_t GLEWGraphicsAPI::LinkProgram(uint32_t vertexShader, uint32_t fragmentShader)
{
	GLuint program = glCreateProgram();
	glAttachShader(program, vertexShader);
	glAttachShader(program, fragmentShader);
	glLinkProgram(program);

	GLint success = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &success);
	if (!success)
	{
		glDeleteProgram(program);
		return 0;
	}

	return program;
}

//=============================================================================
// GPU resource setup
//=============================================================================

void GLEWGraphicsAPI::SetupShaders()
{
	// 2D shader program
	{
		GLuint vs = CompileShader(GL_VERTEX_SHADER, QuadVertexShaderSource);
		GLuint fs = CompileShader(GL_FRAGMENT_SHADER, QuadFragmentShaderSource);
		ShaderProgram2D = LinkProgram(vs, fs);
		glDeleteShader(vs);
		glDeleteShader(fs);

		Uniform2D_Position = glGetUniformLocation(ShaderProgram2D, "uPosition");
		Uniform2D_Scale    = glGetUniformLocation(ShaderProgram2D, "uScale");
		Uniform2D_Rotation = glGetUniformLocation(ShaderProgram2D, "uRotation");
	}

	// 3D shader program
	{
		GLuint vs = CompileShader(GL_VERTEX_SHADER, CubeVertexShaderSource);
		GLuint fs = CompileShader(GL_FRAGMENT_SHADER, CubeFragmentShaderSource);
		ShaderProgram3D = LinkProgram(vs, fs);
		glDeleteShader(vs);
		glDeleteShader(fs);

		Uniform3D_Position = glGetUniformLocation(ShaderProgram3D, "uPosition");
		Uniform3D_Scale    = glGetUniformLocation(ShaderProgram3D, "uScale");
		Uniform3D_Rotation = glGetUniformLocation(ShaderProgram3D, "uRotation");
	}
}

void GLEWGraphicsAPI::SetupQuadGeometry()
{
	glGenVertexArrays(1, &QuadVAO);
	glGenBuffers(1, &QuadVBO);

	glBindVertexArray(QuadVAO);
	glBindBuffer(GL_ARRAY_BUFFER, QuadVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(QuadVertices), QuadVertices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
	glEnableVertexAttribArray(0);

	glBindVertexArray(0);
}

void GLEWGraphicsAPI::SetupCubeGeometry()
{
	glGenVertexArrays(1, &CubeVAO);
	glGenBuffers(1, &CubeVBO);

	glBindVertexArray(CubeVAO);
	glBindBuffer(GL_ARRAY_BUFFER, CubeVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(CubeVertices), CubeVertices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
	glEnableVertexAttribArray(0);

	glBindVertexArray(0);
}

void GLEWGraphicsAPI::CleanupGL()
{
	if (!bInitialized)
	{
		return;
	}

	if (QuadVAO) glDeleteVertexArrays(1, &QuadVAO);
	if (QuadVBO) glDeleteBuffers(1, &QuadVBO);
	if (CubeVAO) glDeleteVertexArrays(1, &CubeVAO);
	if (CubeVBO) glDeleteBuffers(1, &CubeVBO);
	if (ShaderProgram2D) glDeleteProgram(ShaderProgram2D);
	if (ShaderProgram3D) glDeleteProgram(ShaderProgram3D);

	QuadVAO = QuadVBO = 0;
	CubeVAO = CubeVBO = 0;
	ShaderProgram2D = ShaderProgram3D = 0;
	bInitialized = false;
}

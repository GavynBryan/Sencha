#include "MazeRenderer.h"
#include "Maze.h"
#include "Shaders.h"

void MazeRenderer::BuildShader()
{
	GLuint vert = CompileShader(GL_VERTEX_SHADER, Shaders::Vertex);
	GLuint frag = CompileShader(GL_FRAGMENT_SHADER, Shaders::Fragment);
	ShaderProgram = LinkProgram(vert, frag);
	MVPLocation = glGetUniformLocation(ShaderProgram, "uMVP");
}

void MazeRenderer::BuildMesh(const Maze& maze)
{
	std::vector<Vertex> verts;

	int w = maze.GetWidth();
	int h = maze.GetHeight();

	// Colors
	Vec3 wallColor(0.6f, 0.55f, 0.45f);
	Vec3 wallTopColor(0.5f, 0.45f, 0.35f);
	Vec3 floorColor(0.3f, 0.3f, 0.32f);
	Vec3 ceilingColor(0.25f, 0.25f, 0.28f);

	// Normals
	Vec3 up(0.0f, 1.0f, 0.0f);
	Vec3 down(0.0f, -1.0f, 0.0f);
	Vec3 north(0.0f, 0.0f, -1.0f);
	Vec3 south(0.0f, 0.0f, 1.0f);
	Vec3 east(1.0f, 0.0f, 0.0f);
	Vec3 west(-1.0f, 0.0f, 0.0f);

	for (int row = 0; row < h; ++row)
	{
		for (int col = 0; col < w; ++col)
		{
			float x = static_cast<float>(col);
			float z = static_cast<float>(row);

			if (maze.IsWall(row, col))
			{
				float x0 = x, x1 = x + 1.0f;
				float z0 = z, z1 = z + 1.0f;
				float y0 = 0.0f, y1 = 1.0f;

				// Top face (only if visible — always emit for simplicity)
				EmitQuad(verts,
					Vec3(x0, y1, z0), Vec3(x1, y1, z0),
					Vec3(x1, y1, z1), Vec3(x0, y1, z1),
					up, wallTopColor);

				// South face (+Z) — visible if neighbor is passage
				if (maze.IsPassage(row + 1, col))
				{
					EmitQuad(verts,
						Vec3(x0, y0, z1), Vec3(x1, y0, z1),
						Vec3(x1, y1, z1), Vec3(x0, y1, z1),
						south, wallColor);
				}

				// North face (-Z)
				if (maze.IsPassage(row - 1, col))
				{
					EmitQuad(verts,
						Vec3(x1, y0, z0), Vec3(x0, y0, z0),
						Vec3(x0, y1, z0), Vec3(x1, y1, z0),
						north, wallColor);
				}

				// East face (+X)
				if (maze.IsPassage(row, col + 1))
				{
					EmitQuad(verts,
						Vec3(x1, y0, z1), Vec3(x1, y0, z0),
						Vec3(x1, y1, z0), Vec3(x1, y1, z1),
						east, wallColor);
				}

				// West face (-X)
				if (maze.IsPassage(row, col - 1))
				{
					EmitQuad(verts,
						Vec3(x0, y0, z0), Vec3(x0, y0, z1),
						Vec3(x0, y1, z1), Vec3(x0, y1, z0),
						west, wallColor);
				}
			}
			else
			{
				float x0 = x, x1 = x + 1.0f;
				float z0 = z, z1 = z + 1.0f;

				// Floor
				EmitQuad(verts,
					Vec3(x0, 0.0f, z1), Vec3(x1, 0.0f, z1),
					Vec3(x1, 0.0f, z0), Vec3(x0, 0.0f, z0),
					up, floorColor);

				// Ceiling
				EmitQuad(verts,
					Vec3(x0, 1.0f, z0), Vec3(x1, 1.0f, z0),
					Vec3(x1, 1.0f, z1), Vec3(x0, 1.0f, z1),
					down, ceilingColor);
			}
		}
	}

	VertexCount = static_cast<GLsizei>(verts.size());

	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);

	glBindVertexArray(VAO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER,
		static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)),
		verts.data(), GL_STATIC_DRAW);

	// Position (location 0)
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
		reinterpret_cast<void*>(offsetof(Vertex, Position)));

	// Normal (location 1)
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
		reinterpret_cast<void*>(offsetof(Vertex, Normal)));

	// Color (location 2)
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
		reinterpret_cast<void*>(offsetof(Vertex, Color)));

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

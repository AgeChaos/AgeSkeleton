#pragma once

// Opt-in diagnostic used only by OpenHarmony debug templates.
static void run_gles_sampling_probe(GLuint p_texture) {
	static GLuint program = 0;
	static GLuint vao = 0;
	static GLuint vertices = 0;
	if (!program) {
		const char *vertex_source = "#version 300 es\n"
									"layout(location=0) in vec2 position; out vec2 uv; void main() { vec2 p = position;"
									" uv = vec2(p.x, 1.0-p.y); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }";
		const char *fragment_source = "#version 300 es\nprecision highp float;\n"
									  "in vec2 uv; uniform sampler2D source; layout(location=0) out vec4 frag_color;"
									  "void main() { if (uv.y < 0.05) { frag_color=vec4(1,0,1,1); }"
									  "else if (uv.x < 0.5) { frag_color=texture(source,uv); }"
									  "else { frag_color=texelFetch(source,ivec2(uv*vec2(textureSize(source,0))),0); } }";
		GLuint vs = glCreateShader(GL_VERTEX_SHADER);
		GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(vs, 1, &vertex_source, nullptr);
		glShaderSource(fs, 1, &fragment_source, nullptr);
		glCompileShader(vs);
		glCompileShader(fs);
		program = glCreateProgram();
		glAttachShader(program, vs);
		glAttachShader(program, fs);
		glLinkProgram(program);
		glDeleteShader(vs);
		glDeleteShader(fs);
		GLint linked = 0;
		glGetProgramiv(program, GL_LINK_STATUS, &linked);
		if (!linked) {
			char log[1024] = {};
			glGetProgramInfoLog(program, sizeof(log), nullptr, log);
			ERR_PRINT(String("OHOS_SAMPLE link failed: ") + log);
			glDeleteProgram(program);
			program = 0;
			return;
		}
		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vertices);
		GLint previous_buffer = 0;
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous_buffer);
		glBindBuffer(GL_ARRAY_BUFFER, vertices);
		const float points[] = { 0, 0, 2, 0, 0, 2 };
		glBufferData(GL_ARRAY_BUFFER, sizeof(points), points, GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, previous_buffer);
	}
	GLint old_program = 0, old_vao = 0, old_active = 0, old_texture = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vao);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
	glBindTexture(GL_TEXTURE_2D, p_texture);
	const GLenum capabilities[] = { GL_BLEND, GL_DEPTH_TEST, GL_STENCIL_TEST, GL_SCISSOR_TEST, GL_CULL_FACE, GL_RASTERIZER_DISCARD };
	GLboolean enabled[6];
	GLint color_mask[4] = { 1, 1, 1, 1 };
	glGetIntegerv(GL_COLOR_WRITEMASK, color_mask);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	for (int i = 0; i < 6; i++) {
		enabled[i] = glIsEnabled(capabilities[i]);
		glDisable(capabilities[i]);
	}
	glUseProgram(program);
	glUniform1i(glGetUniformLocation(program, "source"), 0);
	glBindVertexArray(vao);
	GLint previous_buffer = 0;
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, vertices);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
	glVertexAttribDivisor(0, 0);
	for (int i = 1; i < 16; i++) {
		glDisableVertexAttribArray(i);
	}
	glDrawArrays(GL_TRIANGLES, 0, 3);
	static uint32_t frame = 0;
	if (++frame == 120) {
		print_line(vformat("OHOS_SAMPLE color_mask=%d,%d,%d,%d discard=%d draw_error=%d", color_mask[0], color_mask[1], color_mask[2], color_mask[3], enabled[5], glGetError()));
	}
	glBindVertexArray(old_vao);
	glBindBuffer(GL_ARRAY_BUFFER, previous_buffer);
	glUseProgram(old_program);
	glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
	for (int i = 0; i < 6; i++) {
		if (enabled[i]) {
			glEnable(capabilities[i]);
		}
	}
	glBindTexture(GL_TEXTURE_2D, old_texture);
	glActiveTexture(old_active);
}

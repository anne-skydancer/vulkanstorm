import unittest
from apitrace_harness import analyze, fields, parse_call


class TraceTests(unittest.TestCase):
    def run_trace(self, calls, **kwargs):
        lines = [f"{i} @0 {call}" for i, call in enumerate(calls)]
        return analyze(lines, **kwargs)

    def prefix(self):
        return ["wglMakeCurrent(hdc = 0x1, hglrc = 0x2) = TRUE",
                "glActiveTexture(texture = GL_TEXTURE0)"]

    def test_texture_repeats_and_target_separation(self):
        result = self.run_trace(self.prefix() + [
            "glBindTexture(target = GL_TEXTURE_2D, texture = 7)",
            "glBindTexture(target = GL_TEXTURE_CUBE_MAP, texture = 7)",
            "glBindTexture(target = GL_TEXTURE_2D, texture = 7)",
            "glActiveTexture(texture = GL_TEXTURE1)",
            "glBindTexture(target = GL_TEXTURE_2D, texture = 7)"])
        stats = result["contexts"]["2"]["stats"]
        self.assertEqual(stats["texture_bind_same_requested_state"], 1)
        self.assertEqual(stats["texture_bind_unknown_prior_state"], 3)

    def test_delete_name_reuse_is_unknown(self):
        result = self.run_trace(self.prefix() + [
            "glBindTexture(target = GL_TEXTURE_2D, texture = 7)",
            "glDeleteTextures(n = 1, textures = {7})",
            "glActiveTexture(texture = GL_TEXTURE0)",
            "glBindTexture(target = GL_TEXTURE_2D, texture = 7)"])
        self.assertNotIn("texture_bind_same_requested_state", result["contexts"]["2"]["stats"])

    def test_attributes_survive_array_unbind_but_not_vao_change(self):
        pointer = "glVertexAttribPointer(index = 0, size = 3, type = GL_FLOAT, normalized = GL_FALSE, stride = 16, pointer = NULL)"
        result = self.run_trace(self.prefix() + [
            "glBindVertexArray(array = 1)", "glBindBuffer(target = GL_ARRAY_BUFFER, buffer = 8)", pointer,
            "glBindBuffer(target = GL_ARRAY_BUFFER, buffer = 0)", "glUseProgram(program = 3)",
            "glBindBuffer(target = GL_ARRAY_BUFFER, buffer = 8)", pointer,
            "glBindVertexArray(array = 2)", pointer])
        stats = result["contexts"]["2"]["stats"]
        self.assertEqual(stats["attribute_pointer_same_requested_state"], 1)
        self.assertEqual(stats["attribute_pointer_repeat_across_shader_change"], 1)

    def test_contexts_and_failed_switch(self):
        result = analyze([
            "0 @0 wglMakeCurrent(hdc = 1, hglrc = 2) = TRUE",
            "1 @0 glActiveTexture(texture = GL_TEXTURE0)",
            "2 @0 glBindTexture(target = GL_TEXTURE_2D, texture = 7)",
            "3 @1 wglMakeCurrent(hdc = 3, hglrc = 4) = TRUE",
            "4 @1 glActiveTexture(texture = GL_TEXTURE0)",
            "5 @1 glBindTexture(target = GL_TEXTURE_2D, texture = 7)",
            "6 @0 wglMakeCurrent(hdc = 3, hglrc = 4) = FALSE",
            "7 @0 glBindTexture(target = GL_TEXTURE_2D, texture = 7)"])
        self.assertEqual(result["contexts"]["2"]["stats"]["texture_bind_same_requested_state"], 1)
        self.assertNotIn("texture_bind_same_requested_state", result["contexts"]["4"]["stats"])

    def test_warmup_preserved_for_selected_frames(self):
        result = self.run_trace(self.prefix() + [
            "glBindTexture(target = GL_TEXTURE_2D, texture = 7)", "SwapBuffers(hdc = 1) = TRUE",
            "glBindTexture(target = GL_TEXTURE_2D, texture = 7)", "SwapBuffers(hdc = 1) = TRUE",
            "glBindTexture(target = GL_TEXTURE_2D, texture = 9)"], start_frame=1, end_frame=2)
        stats = result["contexts"]["2"]["stats"]
        self.assertEqual(stats["texture_bind_calls"], 1)
        self.assertEqual(stats["texture_bind_same_requested_state"], 1)

    def test_unknown_mutation_invalidates(self):
        result = self.run_trace(self.prefix() + [
            "glBindTexture(target = GL_TEXTURE_2D, texture = 7)", "glBindTextures(first = 0, count = 1, textures = {8})",
            "glActiveTexture(texture = GL_TEXTURE0)", "glBindTexture(target = GL_TEXTURE_2D, texture = 7)"])
        self.assertNotIn("texture_bind_same_requested_state", result["contexts"]["2"]["stats"])

    def test_parser_quoted_parentheses_and_arrays(self):
        parsed = parse_call('8 @a glGetString(name = GL_RENDERER) = "zink Vulkan (AMD)"')
        self.assertEqual(parsed[-1], '"zink Vulkan (AMD)"')
        self.assertEqual(fields('n = 2, names = {1, 2}')["names"], "{1, 2}")

    def test_rejects_partial_or_threadless_input(self):
        with self.assertRaises(ValueError):
            analyze(["0 glBindTexture(target = GL_TEXTURE_2D, texture = 7)"])
        with self.assertRaises(ValueError):
            analyze(["0 @0 glBindTexture(target = GL_TEXTURE_2D, texture = 7)"])

    def test_synthetic_calls_are_not_counted(self):
        result = self.run_trace(self.prefix() + [
            "glBindTexture(target = GL_TEXTURE_2D, texture = 7) // fake",
            "glActiveTexture(texture = GL_TEXTURE0)",
            "glBindTexture(target = GL_TEXTURE_2D, texture = 7)"])
        stats = result["contexts"]["2"]["stats"]
        self.assertEqual(stats["texture_bind_calls"], 1)
        self.assertNotIn("texture_bind_same_requested_state", stats)

    def test_resource_generation_preserves_bound_vao(self):
        pointer = "glVertexAttribPointer(index = 0, size = 3, type = GL_FLOAT, normalized = GL_FALSE, stride = 16, pointer = NULL)"
        result = self.run_trace(self.prefix() + [
            "glBindVertexArray(array = 1)", "glGenBuffers(n = 1, buffers = &8)",
            "glBindBuffer(target = GL_ARRAY_BUFFER, buffer = 8)", pointer, pointer,
            "glDeleteTextures(n = 1, textures = &7)", pointer])
        self.assertEqual(result["contexts"]["2"]["stats"]["attribute_pointer_same_requested_state"], 2)

    def test_shader_linkage_and_synthetic_linkage_preserve_bindings(self):
        pointer = "glVertexAttribPointer(index = 0, size = 3, type = GL_FLOAT, normalized = GL_FALSE, stride = 16, pointer = NULL)"
        result = self.run_trace(self.prefix() + [
            "glBindVertexArray(array = 1)", "glBindBuffer(target = GL_ARRAY_BUFFER, buffer = 8)", pointer,
            "glBindTexture(target = GL_TEXTURE_2D, texture = 7)",
            'glBindAttribLocation(program = 4, index = 0, name = "position")',
            'glBindAttribLocation(program = 4, index = 0, name = "position") // fake',
            "glBindImageTexture(unit = 0, texture = 9, level = 0, layered = GL_FALSE, layer = 0, access = GL_READ_WRITE, format = GL_R32UI)",
            pointer, "glBindTexture(target = GL_TEXTURE_2D, texture = 7)"])
        stats = result["contexts"]["2"]["stats"]
        self.assertEqual(stats["attribute_pointer_same_requested_state"], 1)
        self.assertEqual(stats["texture_bind_same_requested_state"], 1)

    def test_worker_context_creation_does_not_reset_main_context(self):
        pointer = "glVertexAttribPointer(index = 0, size = 3, type = GL_FLOAT, normalized = GL_FALSE, stride = 16, pointer = NULL)"
        result = self.run_trace(self.prefix() + [
            "glBindVertexArray(array = 1)", "glBindBuffer(target = GL_ARRAY_BUFFER, buffer = 8)", pointer,
            "wglCreateContextAttribsARB(hDC = 9, hShareContext = 2, attribList = {}) = 3", pointer,
            "wglDeleteContext(hglrc = 3) = TRUE", pointer])
        self.assertEqual(result["contexts"]["2"]["stats"]["attribute_pointer_same_requested_state"], 2)


if __name__ == "__main__":
    unittest.main()

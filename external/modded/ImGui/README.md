# Recubin's Dear ImGui GLFW backend

Base: `external/imgui` at `6b61e60e221817a5084c4576cc5bc42919142c5e`.
The two files add `ImGui_ImplGlfw_InvalidateMouseCursor()` so Recubin can
invalidate the backend's cache after changing the GLFW cursor.

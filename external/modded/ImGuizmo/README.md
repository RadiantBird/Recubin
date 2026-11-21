# Recubin ImGuizmo variant

This directory preserves the ImGuizmo sources used by Recubin after applying
the project-specific gizmo changes. The upstream reference is
`CedricGuillemet/ImGuizmo` at commit
`a15acd87a3f3241a29ea1363ceafc680dca3a96b` (the copy in
`temp_libs/ImGuizmo`).

The Recubin variant adds scale drag sign state and the public helpers
`IsScaleNegative()` and `GetScaleGrabSign()`. It also contains the associated
negative-axis scale and hit-test changes. Keep these files paired: the header
declares the helpers implemented by `ImGuizmo.cpp`.

"""Offline baking helpers for Patronus VFX assets.

Pure numpy; no third-party noise library. Everything here produces raw
texture data the renderer uploads verbatim (see rawtexture.py for the
.bin/.json convention), plus PNG previews for eyeballing before upload.
"""

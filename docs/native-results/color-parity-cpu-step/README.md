# Retained intermediate color comparison

CPU byte quantization reduced the original primitive discrepancy from 4,591
pixels to one red channel at pixel (58,162): C=219, Rust=220. This remains a
failed exact comparison. Texture and cube scenes still matched exactly.
The final shader-normalization fix is documented in `../color-parity-step/`.

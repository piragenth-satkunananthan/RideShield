#!/usr/bin/env python3
"""
RideShield Pothole Model Conversion Script
Exports best.pt (YOLOv12 with Attention patch) to phone-compatible ONNX format.
"""

import argparse
import os
import shutil
import sys
import torch

# Apply YOLOv12 AAttn compatibility patch
import ultralytics.nn.modules.block as ultralytics_block

def patched_aattn_forward(self, x):
    B, C, H, W = x.shape
    N = H * W

    if hasattr(self, "qkv"):
        qkv = self.qkv(x)
        qk_feat = qkv[:, :2 * C, :, :]
        v = qkv[:, 2 * C:, :, :]
        pp = self.pe(v)
        qk = qk_feat.flatten(2).transpose(1, 2)
        v = v.flatten(2).transpose(1, 2)
    else:
        qk = self.qk(x).flatten(2).transpose(1, 2)
        v = self.v(x)
        pp = self.pe(v)
        v = v.flatten(2).transpose(1, 2)

    if self.area > 1:
        qk = qk.reshape(B * self.area, N // self.area, C * 2)
        v = v.reshape(B * self.area, N // self.area, C)
        B, N, _ = qk.shape

    q, k = qk.split([C, C], dim=2)
    q = q.transpose(1, 2).view(B, self.num_heads, self.head_dim, N)
    k = k.transpose(1, 2).view(B, self.num_heads, self.head_dim, N)
    v = v.transpose(1, 2).view(B, self.num_heads, self.head_dim, N)

    attn = (q.transpose(-2, -1) @ k) * (self.head_dim ** -0.5)
    max_attn = attn.max(dim=-1, keepdim=True).values
    exp_attn = torch.exp(attn - max_attn)
    attn = exp_attn / exp_attn.sum(dim=-1, keepdim=True)

    x = v @ attn.transpose(-2, -1)
    x = x.permute(0, 3, 1, 2)

    if self.area > 1:
        x = x.reshape(B // self.area, N * self.area, C)
        B, N, _ = x.shape

    x = x.reshape(B, H, W, C).permute(0, 3, 1, 2)
    return self.proj(x + pp)

ultralytics_block.AAttn.forward = patched_aattn_forward
from ultralytics import YOLO

def main():
    parser = argparse.ArgumentParser(description="Export RideShield best.pt to ONNX")
    parser.add_argument("--input", default="../Pothole-Computer-Vision-Project/best.pt", help="Path to best.pt")
    parser.add_argument("--output", default="app/src/main/assets/models/best.onnx", help="Destination ONNX path")
    parser.add_argument("--imgsz", type=int, default=640, help="Input image size")
    parser.add_argument("--opset", type=int, default=12, help="ONNX opset version")
    args = parser.parse_args()

    if not os.path.exists(args.input):
        print(f"Error: input file {args.input} does not exist", file=sys.stderr)
        sys.exit(1)

    print(f"Loading model from {args.input}...")
    model = YOLO(args.input)
    print(f"Model classes: {model.names}")

    print(f"Exporting to ONNX (imgsz={args.imgsz}, opset={args.opset})...")
    exported_file = model.export(format="onnx", imgsz=args.imgsz, opset=args.opset)
    print(f"ONNX exported to: {exported_file}")

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    shutil.copyfile(exported_file, args.output)
    print(f"Successfully copied to destination: {args.output}")

if __name__ == "__main__":
    main()

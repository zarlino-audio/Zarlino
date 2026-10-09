#!/usr/bin/env python3
import numpy as np

def sat_wavefold(x, drive, asym):
    g = 1.0 + drive * 12.0
    a = asym * 0.4
    x = np.asarray(x, dtype=np.float64) * g * 0.35
    x = x + a * 0.15
    y = np.sin(x * np.pi * 1.5) * 0.8
    y = y * 0.85 + x * 0.15 * (1.0 - np.abs(a))
    return y * 1.2

def downsamp(x, os):
    half = 16 * os
    t = np.arange(-half, half + 1) / os
    k = np.sinc(t) * np.sinc(t / 8.0)
    n = (len(x) // os) * os
    return np.convolve(x, k, mode='same')[:n:os] / os

def upsamp(x, os):
    n = len(x)
    up = np.zeros(n * os)
    up[::os] = x
    half = 16 * os
    t = np.arange(-half, half + 1) / os
    k = np.sinc(t) * np.sinc(t / 8.0)
    return np.convolve(up, k, mode='same')[:n * os] * os

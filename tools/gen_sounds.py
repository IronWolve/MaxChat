"""
gen_sounds.py — Generate notification sound WAV files for MaxChat.
Uses only Python stdlib: wave, struct, math.
Output: assets/sounds/{bell,ping,scifi_chirp,trek_hail}.wav
"""

import wave
import struct
import math
import os

SAMPLE_RATE = 44100
CHANNELS = 1
SAMPWIDTH = 2  # 16-bit


def write_wav(path, samples):
    """Write a list of float samples (range ~[-1, 1]) as 16-bit PCM WAV."""
    with wave.open(path, 'w') as wf:
        wf.setnchannels(CHANNELS)
        wf.setsampwidth(SAMPWIDTH)
        wf.setframerate(SAMPLE_RATE)
        frames = struct.pack('<' + 'h' * len(samples),
                             *[max(-32768, min(32767, int(s))) for s in samples])
        wf.writeframes(frames)


def normalize(samples, peak=0.85):
    """Scale samples so the absolute peak equals `peak` of full scale (32767)."""
    max_val = max(abs(s) for s in samples)
    if max_val == 0:
        return samples
    scale = peak * 32767.0 / max_val
    return [s * scale for s in samples]


# ---------------------------------------------------------------------------
# 1. bell.wav
#    Real struck-bell via additive inharmonic partials + per-partial decay.
# ---------------------------------------------------------------------------
def gen_bell():
    dur = 0.55  # 0.55 s keeps file under 50 KB at 44100 Hz 16-bit mono
    f0 = 587.0  # D5
    sr = SAMPLE_RATE
    n = int(dur * sr)
    attack_samples = int(0.003 * sr)  # 3 ms

    # Tuned-bell inharmonic partial ratios and relative amplitudes
    ratios = [1.0, 2.76, 5.40, 8.93, 13.34]
    amps   = [1.0, 0.6,  0.4,  0.25, 0.15]

    samples = []
    for i in range(n):
        t = i / sr
        # Global linear attack ramp
        if i < attack_samples:
            attack = i / attack_samples
        else:
            attack = 1.0
        # 2 Hz shimmer beat
        shimmer = 1.0 + 0.08 * math.sin(2 * math.pi * 2 * t)
        v = 0.0
        for k, (ratio, amp) in enumerate(zip(ratios, amps)):
            freq = f0 * ratio
            decay_k = math.exp(-t * (4.0 + 1.5 * k))
            v += amp * decay_k * math.sin(2 * math.pi * freq * t)
        samples.append(attack * shimmer * v)

    return normalize(samples, peak=0.85)


# ---------------------------------------------------------------------------
# 2. ping.wav
#    Gentle water-drop: upward frequency glide 660->880 Hz + 2nd harmonic.
# ---------------------------------------------------------------------------
def gen_ping():
    dur = 0.25
    sr = SAMPLE_RATE
    n = int(dur * sr)
    attack_samples = int(0.005 * sr)  # 5 ms

    samples = []
    phase = 0.0
    phase2 = 0.0
    for i in range(n):
        t = i / sr
        # Instantaneous freq glide
        f = 660.0 + 220.0 * (t / dur)
        phase += 2 * math.pi * f / sr
        phase2 += 2 * math.pi * (2 * f) / sr

        if i < attack_samples:
            env = i / attack_samples
        else:
            env = math.exp(-t * 14.0)

        v = math.sin(phase) + 0.2 * math.sin(phase2)
        samples.append(env * v)

    return normalize(samples, peak=0.5)


# ---------------------------------------------------------------------------
# 3. scifi_chirp.wav
#    Fast rising laser chirp: square-ish tone (odd harmonics) + vibrato,
#    exponential freq sweep 400->1600 Hz.
# ---------------------------------------------------------------------------
def gen_scifi_chirp():
    dur = 0.18
    sr = SAMPLE_RATE
    n = int(dur * sr)
    attack_samples = int(0.003 * sr)   # 3 ms
    release_samples = int(0.025 * sr)  # 25 ms

    # Odd harmonic amplitudes (fake square, no aliasing at these freqs)
    harm_amps = [(1, 1.0), (3, 0.33), (5, 0.2)]

    samples = []
    # Accumulate phase per harmonic separately
    phases = [0.0, 0.0, 0.0]
    for i in range(n):
        t = i / sr
        # Exponential sweep: f(t) = 400 * 4^(t/dur)
        f_base = 400.0 * (4.0 ** (t / dur))
        # 30 Hz vibrato
        vib = 1.0 + 0.03 * math.sin(2 * math.pi * 30 * t)
        f_inst = f_base * vib

        # Envelope
        if i < attack_samples:
            env = i / attack_samples
        elif i >= n - release_samples:
            env = (n - i) / release_samples
        else:
            env = 1.0

        v = 0.0
        for idx, (harmonic, amp) in enumerate(harm_amps):
            phases[idx] += 2 * math.pi * harmonic * f_inst / sr
            v += amp * math.sin(phases[idx])
        samples.append(env * v)

    return normalize(samples, peak=0.7)


# ---------------------------------------------------------------------------
# 4. trek_hail.wav
#    Star-Trek-style two-tone communicator chirp:
#    Segment A (700 Hz, 0.18 s) | 30 ms silence | Segment B (933 Hz, 0.24 s)
# ---------------------------------------------------------------------------
def gen_trek_hail():
    sr = SAMPLE_RATE
    attack_r = int(0.008 * sr)   # 8 ms
    release_r = int(0.008 * sr)  # 8 ms

    def tone_segment(freq, dur, overtone_freq=None, overtone_amp=0.0):
        n = int(dur * sr)
        seg = []
        phase = 0.0
        phase_ov = 0.0
        for i in range(n):
            t = i / sr
            phase += 2 * math.pi * freq / sr
            phase_ov += 2 * math.pi * (overtone_freq or freq) / sr
            if i < attack_r:
                env = i / attack_r
            elif i >= n - release_r:
                env = (n - i) / release_r
            else:
                env = 1.0
            v = math.sin(phase)
            if overtone_freq:
                v += overtone_amp * math.sin(phase_ov)
            seg.append(env * v)
        return seg

    seg_a = tone_segment(700.0, 0.18)
    gap   = [0.0] * int(0.03 * sr)   # 30 ms silence
    seg_b = tone_segment(933.0, 0.24, overtone_freq=1568.0, overtone_amp=0.12)

    combined = seg_a + gap + seg_b
    return normalize(combined, peak=0.75)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_dir = os.path.join(root, 'assets', 'sounds')
    os.makedirs(out_dir, exist_ok=True)

    sounds = {
        'bell.wav':       gen_bell,
        'ping.wav':       gen_ping,
        'scifi_chirp.wav': gen_scifi_chirp,
        'trek_hail.wav':  gen_trek_hail,
    }

    for filename, generator in sounds.items():
        path = os.path.join(out_dir, filename)
        print(f'Generating {filename} ...', end=' ', flush=True)
        samples = generator()
        write_wav(path, samples)
        size_kb = os.path.getsize(path) / 1024
        print(f'done  ({size_kb:.1f} KB,  {len(samples)} samples)')

    print('All sounds written to', out_dir)


if __name__ == '__main__':
    main()

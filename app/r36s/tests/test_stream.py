"""Real local WebRTC -> H.264/Opus -> SDL, plus input received by Pion.

Usage: python3 test_stream.py /path/to/orbita /path/to/peer_pion
No Microsoft traffic, token store, provider account or recorded gameplay.
"""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


with tempfile.TemporaryDirectory(prefix="orbita-stream-") as tmp:
    root = Path(tmp)
    sample = root / "sample.h264"
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "testsrc2=size=640x360:rate=30", "-t", "2", "-c:v", "libx264",
                    "-preset", "ultrafast", "-tune", "zerolatency", "-profile:v", "baseline",
                    "-g", "30", "-x264-params", "aud=1:repeat-headers=1", "-f", "h264", str(sample)], check=True)
    with (root / "peer.log").open("w") as peer_log:
        peer = subprocess.Popen([sys.argv[2], "-dir", tmp, "-h264", str(sample),
                                 "-seconds", "15", "-opus-silence"], stdout=peer_log, stderr=subprocess.STDOUT)
        try:
            env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                       ORBITA_LOCAL_IP="127.0.0.1")
            run = subprocess.run([sys.argv[1], "--stream-test-dir", tmp, "--test-seconds", "7"],
                                 env=env, capture_output=True, text=True, timeout=30)
            log = run.stdout + run.stderr
            assert run.returncode == 0, "exit=%d\n%s" % (run.returncode, log)
            assert "pedido=1280x720 fps=30 bitrate_kbps=1000" in log, log
            offer = (root / "offer.sdp").read_text()
            assert "b=AS:1000" in offer and "b=TIAS:1000000" in offer, offer
            peer.wait(timeout=10)
            assert peer.returncode == 0, (root / "peer.log").read_text()
            assert "video_readback=1" in log, log
            final = re.search(r"terminou decoded=(\d+) audio=(\d+) audio_errors=(\d+)", log)
            assert final and int(final[1]) > 20 and int(final[2]) > 20 and int(final[3]) == 0, log
            stats = json.loads((root / "pion.json").read_text())
            assert stats["handshake_seen"], stats
            assert stats["input_packets"] > 20 and stats["input_bad_len"] == 0 and stats["input_seq_gaps"] == 0, stats
            assert stats["input_a_toggles"] > 3 and stats["input_min_lx"] < 0 < stats["input_max_lx"], stats
            assert stats["rtcp_pli"] > 0 and stats["rtcp_remb"] > 0, stats
            assert abs(stats["remb_bps_last"] - 1_000_000) < 8, stats
            print("WebRTC local: %s decoded frames, %s Opus packets, SDL readback OK; %d valid input packets" %
                  (final[1], final[2], stats["input_packets"]))
        finally:
            if peer.poll() is None:
                peer.terminate()
                try:
                    peer.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    peer.kill()
                    peer.wait()

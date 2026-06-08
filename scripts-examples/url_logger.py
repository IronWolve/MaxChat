"""URL logger — records every URL seen in chat to ``urls.log`` in the script data folder.

A working example of the ``on_message`` hook. Open the log via Settings ▸ Scripts… ▸ Open folder
(it's in the ``data`` subfolder). Turn it off with ``/unload url_logger`` or by deleting this file.
"""

import os
import re
from datetime import datetime

_URL = re.compile(r"https?://[^\s<>\"']+")


def on_load(api):
    api.echo(f"[url_logger] active — logging URLs to {os.path.join(api.data_dir(), 'urls.log')}")


def on_message(api, network, target, nick, text):
    urls = _URL.findall(text)
    if not urls:
        return
    with open(os.path.join(api.data_dir(), "urls.log"), "a", encoding="utf-8") as f:
        for url in urls:
            f.write(f"{datetime.now():%Y-%m-%d %H:%M:%S}\t{network}/{target}\t<{nick}>\t{url}\n")

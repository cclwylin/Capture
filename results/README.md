Generated results live in `/workspace/verified-batch/results.json` and `results.tsv`.
`/workspace/verified-batch/report.md` links to every replay PNG and capture.

For GitHub, [report.md](report.md) contains all 99 outcomes and repository-local
text logs. [gallery.md](gallery.md) indexes 95 PNGs; binary files belong to the
GitHub Release, whose verified publication state is in [release.json](release.json).
[artifacts.json](artifacts.json) records 96 RDC hashes and image/reference hashes.

After updating a batch, run `python3 report.py` then
`python3 tools/publish_results.py` from `/workspace/Capture`.
The publisher validates RGB equality and refreshes text metadata; it does not
capture, replay, upload Release assets, or push Git.

The earlier `/workspace/captures` directory contains initialization-only captures
and must not be used as evidence that the examples rendered successfully.

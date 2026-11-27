"""MkDocs hooks: make the links that leave docs/ work on the published site.

MkDocs only publishes docs_dir, so a link such as ../README.md or ../src/app/main.cpp
is a dead link on the site. Two things are done here:

- the root README.md and CONTRIBUTING.md are published as pages (under project/);
- every other link to a file or folder outside docs/ is pointed at the repository
  on GitHub.
"""

import os
import posixpath
import re

from mkdocs.structure.files import File

# Root files published as pages: path in the repository -> path on the site.
PUBLISHED_ROOT_FILES = {
    "README.md": "project/README.md",
    "CONTRIBUTING.md": "project/CONTRIBUTING.md",
}
SITE_TO_REPO = {site: repo for repo, site in PUBLISHED_ROOT_FILES.items()}

GITHUB_BRANCH = "master"
LINK = re.compile(r"(?<!!)\]\(([^)\s]+)\)")
FENCE = re.compile(r"^(```|~~~)")


def _repo_root(config):
    return os.path.dirname(os.path.abspath(config["config_file_path"]))


def on_files(files, config):
    root = _repo_root(config)
    for repo_path, site_path in PUBLISHED_ROOT_FILES.items():
        with open(os.path.join(root, repo_path), encoding="utf-8") as source:
            files.append(File.generated(config, site_path, content=source.read()))
    return files


def _rewrite(target, page_src, page_repo_dir, config):
    """The link target as it must read on the site, or the target unchanged."""
    if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target) or target.startswith(("#", "/")):
        return target
    path, hash_mark, anchor = target.partition("#")
    if not path:
        return target
    repo_path = posixpath.normpath(posixpath.join(page_repo_dir, path))
    if repo_path.startswith(".."):
        return target
    full_path = os.path.join(_repo_root(config), repo_path)
    if not os.path.exists(full_path):
        return target  # left for MkDocs to report

    page_site_dir = posixpath.dirname(page_src) or "."
    if repo_path in PUBLISHED_ROOT_FILES:
        site_target = PUBLISHED_ROOT_FILES[repo_path]
    elif repo_path == "docs" or repo_path.startswith("docs/"):
        if page_src not in SITE_TO_REPO:
            return target  # a link inside docs/ from a page of docs/: already right
        site_target = repo_path[len("docs/"):]
        if os.path.isdir(full_path):
            site_target = posixpath.join(site_target, "README.md")
    else:
        kind = "tree" if os.path.isdir(full_path) else "blob"
        return f"{config['repo_url'].rstrip('/')}/{kind}/{GITHUB_BRANCH}/{repo_path}{hash_mark}{anchor}"
    return posixpath.relpath(site_target, page_site_dir) + hash_mark + anchor


def on_page_markdown(markdown, page, config, files):
    page_src = page.file.src_uri
    if page_src in SITE_TO_REPO:
        page_repo_dir = posixpath.dirname(SITE_TO_REPO[page_src])
    else:
        page_repo_dir = posixpath.dirname(posixpath.join("docs", page_src))

    lines = []
    in_code_block = False
    for line in markdown.split("\n"):
        if FENCE.match(line.lstrip()):
            in_code_block = not in_code_block
        elif not in_code_block:
            line = LINK.sub(
                lambda match: "](" + _rewrite(match.group(1), page_src, page_repo_dir, config) + ")",
                line,
            )
        lines.append(line)
    return "\n".join(lines)

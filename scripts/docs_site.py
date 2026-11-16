#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0
"""Assembles the docs site's sources into one folder for Zensical, from the files that are already kept up to date:

- website/: the site's own pages (home, install, how it works) and zensical.toml, whose nav this fills in;
- the in-app Help guides (server/internal/admin/help), as Get started and How-to guides, with their pictures;
- the firmware, server and protocol guides (docs/*/README.md), split into one reference page per section;
- the board list and the test reports.

Nothing is written twice by hand: the Help in the web view and the site stay the same. Links are rewritten for the site:
within a split guide to the page that now holds the section, to another guide's section likewise, and to anything else in
the repository to its page on GitHub.

Usage: docs_site.py <repository> <output folder>
"""
import re
import shutil
import sys
from pathlib import Path

GITHUB = "https://github.com/jvrpapa05"
# The site is published from the public firmware repository. The server's code is in its private repository, so a link to
# it from the site would be broken for readers: it becomes plain text.
PRIVATE_PATHS = ("server/", "tools/mcp/")

# The Help guides, in the order a new user meets them. The first five are the getting-started path.
GET_STARTED = [
    "01-getting-started",
    "02-add-device",
    "03-firmware",
    "05-symbol-files",
    "06-crashes",
]
GUIDES = [
    "07-updates",
    "04-port-to-a-board",
    "08-metrics",
    "09-logs",
    "10-accounts",
    "11-remote-access",
    "12-troubleshooting",
]

# The guides split into reference pages: source, site folder, title of their section in the navigation.
REFERENCE = [
    ("docs/firmware/README.md", "reference/firmware", "Firmware"),
    ("docs/server/README.md", "reference/server", "Server"),
    ("docs/protocol/README.md", "reference/protocol", "Protocol"),
]


def slugify(text):
    """Python-Markdown's anchor for a heading, as Zensical makes them."""
    text = re.sub(r"`|\*|_(?=\w)|(?<=\w)_", "", text)
    text = re.sub(r"[^\w\s-]", "", text).strip().lower()
    return re.sub(r"[-\s]+", "-", text)


def title_of(markdown):
    m = re.search(r"^# (.+)$", markdown, re.M)
    return m.group(1).strip() if m else "Untitled"


def split_guide(text):
    """A guide's introduction and its "## " sections, as (title, body) pairs; code blocks are left alone."""
    lines, sections, current, fence = text.splitlines(), [], ("", []), False
    for line in lines:
        if line.startswith("```"):
            fence = not fence
        if not fence and line.startswith("## "):
            sections.append(current)
            current = (line[3:].strip(), [])
            continue
        current[1].append(line)
    sections.append(current)
    return sections


def github_url(path):
    """A repository file's page on GitHub, or None for the server's code, whose repository is private."""
    if path.startswith(PRIVATE_PATHS):
        return None
    kind = "tree" if not Path(path).suffix else "blob"
    return f"{GITHUB}/tracegale-firmware/{kind}/main/{path}"


class Site:
    def __init__(self, repo, out):
        self.repo, self.out = repo, out
        # (source path, anchor) -> site page and anchor, for every section of every split guide.
        self.anchors = {}
        # Source file -> site page, for whole files.
        self.pages = {}
        self.nav = {}

    def write(self, page, text):
        target = self.out / page
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text)

    def relative(self, from_page, to_page):
        return Path(*[".."] * (len(Path(from_page).parts) - 1), to_page).as_posix() if to_page else ""

    def rewrite_links(self, text, source, page):
        """Rewrites a page's links from the repository's layout to the site's."""
        source_dir = Path(source).parent

        def fix(m):
            label, target = m.group(1), m.group(2)
            if re.match(r"[a-z]+:", target):
                return m.group(0)
            path_part, _, anchor = target.partition("#")
            if not path_part:
                resolved = source
            else:
                resolved = (source_dir / path_part).as_posix()
                resolved = str(Path(resolved)).replace("\\", "/")
                parts = []
                for part in resolved.split("/"):
                    if part == "..":
                        parts.pop() if parts else None
                    elif part != ".":
                        parts.append(part)
                resolved = "/".join(parts)
            if anchor and (resolved, anchor) in self.anchors:
                to_page, to_anchor = self.anchors[(resolved, anchor)]
                link = self.relative(page, to_page) + (f"#{to_anchor}" if to_anchor else "")
                return f"[{label}]({link})"
            if resolved in self.pages:
                link = self.relative(page, self.pages[resolved]) + (f"#{anchor}" if anchor else "")
                return f"[{label}]({link})"
            if (self.repo / resolved).exists():
                url = github_url(resolved)
                if url is None:
                    return label
                return f"[{label}]({url}{'#' + anchor if anchor else ''})"
            return m.group(0)

        return re.sub(r"(?<!!)\[([^\]]*)\]\(([^)\s]+)\)", fix, text)

    def plan_reference(self):
        """Decides every reference page and the anchors each section moves to, before any page is written."""
        for source, folder, _ in REFERENCE:
            text = (self.repo / source).read_text()
            self.pages[source] = f"{folder}/index.md"
            for title, body in split_guide(text)[1:]:
                page = f"{folder}/{slugify(title)}.md"
                self.anchors[(source, slugify(title))] = (page, "")
                fence = False
                for line in body:
                    if line.startswith("```"):
                        fence = not fence
                    if not fence and line.startswith("### "):
                        self.anchors[(source, slugify(line[4:]))] = (page, slugify(line[4:]))

    def reference(self):
        nav = []
        for source, folder, label in REFERENCE:
            text = (self.repo / source).read_text()
            sections = split_guide(text)
            intro_title = title_of(text)
            intro_body = "\n".join(line for line in sections[0][1] if not line.startswith("# "))
            entries = [f'"{folder}/index.md"']
            listing = "\n".join(f"- [{t}]({slugify(t)}.md)" for t, _ in sections[1:])
            self.write(f"{folder}/index.md",
                       self.rewrite_links(f"# {intro_title}\n{intro_body}\n\n## In this section\n\n{listing}\n", source, f"{folder}/index.md"))
            for title, body in sections[1:]:
                page = f"{folder}/{slugify(title)}.md"
                content = "\n".join(re.sub(r"^###", "##", line) if line.startswith("###") else line for line in body)
                self.write(page, self.rewrite_links(f"# {title}\n{content}\n", source, page))
                entries.append(f'"{page}"')
            nav.append((label, entries))
        return nav

    def help_guides(self):
        help_dir = self.repo / "server/internal/admin/help"
        images = self.out / "assets/help"
        images.mkdir(parents=True, exist_ok=True)
        for picture in (help_dir / "images").iterdir():
            shutil.copy(picture, images / picture.name)
        homes = {name: f"get-started/{name[3:]}.md" for name in GET_STARTED}
        homes |= {name: f"guides/{name[3:]}.md" for name in GUIDES}
        for name, page in homes.items():
            self.pages[f"server/internal/admin/help/{name}.md"] = page
        for name, page in homes.items():
            source = f"server/internal/admin/help/{name}.md"
            text = (help_dir / f"{name}.md").read_text()
            text = re.sub(r"<!-- (order|summary): .*? -->\n", "", text)
            depth = "../" * (len(Path(page).parts) - 1)
            text = re.sub(r"\]\(images/", f"]({depth}assets/help/", text)
            # The in-app Help links its own pages by file name.
            text = re.sub(r"\]\((\d\d-[a-z0-9-]+)\.md(#[^)]*)?\)",
                          lambda m: f"]({self.relative(page, homes.get(m.group(1), ''))}{m.group(2) or ''})", text)
            self.write(page, self.rewrite_links(text, source, page))
        return [f'"get-started/install.md"'] + [f'"{homes[n]}"' for n in GET_STARTED], [f'"{homes[n]}"' for n in GUIDES]

    def boards_and_reports(self):
        self.pages["docs/firmware/boards.md"] = "boards.md"
        reports = sorted((self.repo / "docs/reports").glob("*.md"), reverse=True)
        for report in reports:
            self.pages[f"docs/reports/{report.name}"] = "reports/index.md" if report.name == "README.md" else f"reports/{report.name}"
        self.write("boards.md", self.rewrite_links((self.repo / "docs/firmware/boards.md").read_text(), "docs/firmware/boards.md", "boards.md"))
        entries = []
        for report in reports:
            page = self.pages[f"docs/reports/{report.name}"]
            self.write(page, self.rewrite_links(report.read_text(), f"docs/reports/{report.name}", page))
            entries.append(f'"{page}"')
        return ['"reports/index.md"'] + [e for e in entries if e != '"reports/index.md"']


def includes(site):
    """Replaces "<!-- include: <file>#<section> -->" in the site's own pages with that section of a guide, its subsections
    raised a level, so a page can show, for example, the protocol's diagrams without a copy of them."""
    for page_path in site.out.rglob("*.md"):
        text = page_path.read_text()
        if "<!-- include:" not in text:
            continue
        page = page_path.relative_to(site.out).as_posix()

        def fill(m):
            source, anchor = m.group(1), m.group(2)
            for title, body in split_guide((site.repo / source).read_text())[1:]:
                if slugify(title) == anchor:
                    content = "\n".join(re.sub(r"^###", "##", line) if line.startswith("###") else line for line in body)
                    return site.rewrite_links(content, source, page)
            raise SystemExit(f"{page}: no section {anchor} in {source}")
        page_path.write_text(re.sub(r"<!-- include: ([^#\s]+)#([\w-]+) -->", fill, text))


def toml_nav(sections):
    lines = ["nav = ["]
    for title, entries in sections:
        if isinstance(entries, str):
            lines.append(f'  {{ "{title}" = {entries} }},')
        else:
            lines.append(f'  {{ "{title}" = [')
            for entry in entries:
                if isinstance(entry, tuple):
                    lines.append(f'    {{ "{entry[0]}" = [{", ".join(entry[1])}] }},')
                else:
                    lines.append(f"    {entry},")
            lines.append("  ] },")
    lines.append("]")
    return "\n".join(lines)


def main():
    repo, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    source = repo / "website"
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(source / "docs", out / "docs")
    site = Site(repo, out / "docs")
    site.plan_reference()
    get_started, guides = site.help_guides()
    reports = site.boards_and_reports()
    reference = site.reference()
    includes(site)
    nav = toml_nav([
        ("Home", '"index.md"'),
        ("Get started", get_started),
        ("How-to guides", guides),
        ("How it works", ['"concepts/how-it-works.md"']),
        ("Reference", reference),
        ("Boards", '"boards.md"'),
        ("Test reports", reports),
    ])
    config = (source / "zensical.toml").read_text().replace("\n#@NAV@\n", "\n" + nav + "\n")
    (out / "zensical.toml").write_text(config)
    print(f"Assembled the site's sources in {out}")


if __name__ == "__main__":
    main()

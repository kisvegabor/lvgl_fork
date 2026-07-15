#!/usr/bin/env python3
"""Generate a Software Bill of Materials (SBOM) for LVGL in SPDX 3.0 (JSON-LD).

The component data comes from a single source of truth:
    scripts/third_party.json
which is also used to regenerate COPYRIGHTS.md (see generate_copyrights.py).

The LVGL version is read at run time from include/lvgl/lv_version.h so the
document always matches the checked-out tree.

Output: SPDX 3.0.1 JSON-LD written to the SBOM folder (default: ./sbom).

Usage:
    python3 scripts/generate_sbom.py [--output-dir sbom] [--date 2026-07-15]

SPDX 3.0.1 spec: https://spdx.github.io/spdx-spec/v3.0.1/
"""

import argparse
import datetime
import json
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_FILE = os.path.join(REPO_ROOT, "sbom", "third_party.json")

# Namespace used to mint SPDX element identifiers (URIs). The version is
# appended at run time so different releases produce distinct id spaces.
NS_BASE = "https://lvgl.io/spdx"


def read_version():
    """Return the LVGL version string, e.g. '9.6.0-dev'."""
    path = os.path.join(REPO_ROOT, "include", "lvgl", "lv_version.h")
    with open(path, encoding="utf-8") as fh:
        text = fh.read()

    def grab(macro):
        m = re.search(r"#define\s+%s\s+\"?([^\"\n]+)\"?" % macro, text)
        return m.group(1).strip() if m else None

    version = "%s.%s.%s" % (grab("LVGL_VERSION_MAJOR"),
                            grab("LVGL_VERSION_MINOR"),
                            grab("LVGL_VERSION_PATCH"))
    info = grab("LVGL_VERSION_INFO")
    if info:
        version += "-" + info
    return version


def read_file(rel_path):
    """Return the text of a repo file, or None if it is not present."""
    path = os.path.join(REPO_ROOT, rel_path)
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8", errors="replace") as fh:
        return fh.read()


def build_document(data, version, spec_version, created):
    """Build the SPDX 3.0 JSON-LD document as a Python dict."""
    project = data["project"]
    # Third-party components only (LVGL's own code is part of the root package).
    components = [c for c in data["components"] if c.get("third_party", True)]

    ns = "%s/lvgl-%s" % (NS_BASE, version)

    def sid(fragment):
        return "%s#%s" % (ns, fragment)

    creation_info = "_:creationinfo"
    tool_id = sid("Tool-generate_sbom.py")
    doc_id = sid("SPDXDocument")
    sbom_id = sid("SBOM")
    root_id = sid("Package-%s" % project["name"])

    graph = []

    # --- Agent registry ----------------------------------------------------
    # Suppliers/creators (Person/Organization) are deduplicated by name and
    # emitted once at the end of the graph.
    agents = {}  # name -> {"id": ..., "type": ...}

    def agent_ref(supplier):
        """Register a supplier {name, type} and return its SPDX element id."""
        name = supplier["name"]
        if name not in agents:
            slug = re.sub(r"[^A-Za-z0-9]+", "-", name).strip("-")
            kind = "Organization" if supplier.get("type") == "organization" else "Person"
            agents[name] = {"id": sid("Agent-" + slug), "type": kind}
        return agents[name]["id"]

    creator_id = agent_ref({"name": "LVGL Kft", "type": "organization"})

    # --- CreationInfo (blank node) -----------------------------------------
    graph.append({
        "type": "CreationInfo",
        "@id": creation_info,
        "specVersion": spec_version,
        "created": created,
        "createdBy": [creator_id],
        "createdUsing": [tool_id],
    })
    graph.append({
        "type": "Tool",
        "spdxId": tool_id,
        "creationInfo": creation_info,
        "name": "scripts/generate_sbom.py",
    })

    # --- Licensing elements ------------------------------------------------
    # One LicenseExpression element per distinct expression; reused by relationships.
    all_licenses = sorted({c["license"] for c in components} | {project["license"]})
    license_expr_ids = {}
    for expr in all_licenses:
        frag = "LicenseExpression-" + re.sub(r"[^A-Za-z0-9.]+", "_", expr)
        lid = sid(frag)
        license_expr_ids[expr] = lid
        graph.append({
            "type": "simplelicensing_LicenseExpression",
            "spdxId": lid,
            "creationInfo": creation_info,
            "simplelicensing_licenseExpression": expr,
        })

    # Custom (non-SPDX-list) licenses referenced by a LicenseRef-* need an
    # explicit CustomLicense element so the reference resolves in-document.
    for comp in components:
        if comp["license"].startswith("LicenseRef-") and comp.get("license_file"):
            text = read_file(comp["license_file"]) or "See %s" % comp["license_file"]
            graph.append({
                "type": "expandedlicensing_CustomLicense",
                "spdxId": sid("CustomLicense-" + re.sub(r"[^A-Za-z0-9]+", "_", comp["license"])),
                "creationInfo": creation_info,
                "name": comp["license"],
                # Schema requires the license text under simplelicensing_licenseText.
                "simplelicensing_licenseText": text,
                "expandedlicensing_isOsiApproved": False,
            })

    # --- LVGL root package -------------------------------------------------
    graph.append({
        "type": "software_Package",
        "spdxId": root_id,
        "creationInfo": creation_info,
        "name": project["name"],
        "summary": project.get("summary", ""),
        "software_packageVersion": version,
        "software_downloadLocation": project["download_location"],
        "software_homePage": project.get("homepage", project["download_location"]),
        "software_primaryPurpose": "library",
        "suppliedBy": agent_ref(project["supplier"]),
        "externalIdentifier": [{
            "type": "ExternalIdentifier",
            "externalIdentifierType": "packageUrl",
            "identifier": "pkg:github/lvgl/lvgl@%s" % version,
        }],
    })

    # --- Third-party component packages ------------------------------------
    component_ids = []
    relationships = []
    for comp in components:
        cid = sid("Package-" + comp["key"])
        component_ids.append(cid)

        source_urls = [s["url"] for s in comp.get("sources", [])]
        comment = "In-tree path: %s" % ", ".join(comp["paths"])
        if comp.get("note"):
            comment += " | " + comp["note"]

        pkg = {
            "type": "software_Package",
            "spdxId": cid,
            "creationInfo": creation_info,
            "name": comp["name"],
            "software_primaryPurpose": comp.get("purpose", "library"),
            "comment": comment,
        }
        if comp.get("supplier"):
            pkg["suppliedBy"] = agent_ref(comp["supplier"])
        if source_urls:
            pkg["software_downloadLocation"] = source_urls[0]
            pkg["software_homePage"] = source_urls[0]
        if comp.get("purl"):
            pkg["externalIdentifier"] = [{
                "type": "ExternalIdentifier",
                "externalIdentifierType": "packageUrl",
                "identifier": comp["purl"],
            }]
        graph.append(pkg)

        # lvgl CONTAINS component
        relationships.append({
            "type": "Relationship",
            "spdxId": sid("Relationship-contains-" + comp["key"]),
            "creationInfo": creation_info,
            "from": root_id,
            "relationshipType": "contains",
            "to": [cid],
        })
        # component license (declared == concluded for a vendored copy)
        for rel_type in ("hasDeclaredLicense", "hasConcludedLicense"):
            relationships.append({
                "type": "Relationship",
                "spdxId": sid("Relationship-%s-%s" % (rel_type, comp["key"])),
                "creationInfo": creation_info,
                "from": cid,
                "relationshipType": rel_type,
                "to": [license_expr_ids[comp["license"]]],
            })

    # lvgl's own license
    for rel_type in ("hasDeclaredLicense", "hasConcludedLicense"):
        relationships.append({
            "type": "Relationship",
            "spdxId": sid("Relationship-%s-%s" % (rel_type, project["name"])),
            "creationInfo": creation_info,
            "from": root_id,
            "relationshipType": rel_type,
            "to": [license_expr_ids[project["license"]]],
        })

    graph.extend(relationships)

    # --- Agent elements (creator + package suppliers) ----------------------
    for name, info in agents.items():
        graph.append({
            "type": info["type"],
            "spdxId": info["id"],
            "creationInfo": creation_info,
            "name": name,
        })

    # --- SBOM element ------------------------------------------------------
    graph.append({
        "type": "software_Sbom",
        "spdxId": sbom_id,
        "creationInfo": creation_info,
        "software_sbomType": ["source"],
        "rootElement": [root_id],
        "element": [root_id] + component_ids,
    })

    # --- SpdxDocument (wraps everything) -----------------------------------
    doc_elements = [e.get("spdxId") for e in graph if e.get("spdxId")]
    graph.append({
        "type": "SpdxDocument",
        "spdxId": doc_id,
        "creationInfo": creation_info,
        "name": "LVGL %s SBOM" % version,
        "profileConformance": ["core", "software", "simpleLicensing", "expandedLicensing"],
        "rootElement": [sbom_id],
        "element": doc_elements,
    })

    document = {
        "@context": "https://spdx.org/rdf/%s/spdx-context.jsonld" % spec_version,
        "@graph": graph,
    }
    return document


def _serialize(document):
    return json.dumps(document, indent=2, ensure_ascii=False) + "\n"


def _normalized(document):
    """Copy with the volatile creation timestamp blanked, for drift comparison."""
    doc = json.loads(json.dumps(document))
    for element in doc.get("@graph", []):
        if element.get("type") == "CreationInfo":
            element["created"] = "<normalized>"
    return _serialize(doc)


def main():
    parser = argparse.ArgumentParser(description="Generate an SPDX 3.0 SBOM for LVGL.")
    parser.add_argument("--output-dir", default=os.path.join(REPO_ROOT, "sbom"),
                        help="Directory to write the SBOM into (default: ./sbom).")
    parser.add_argument("--date", default=None,
                        help="Creation date (YYYY-MM-DD). Defaults to today (UTC).")
    parser.add_argument("--spec-version", default="3.0.1",
                        help="SPDX specification version (default: 3.0.1).")
    parser.add_argument("--check", action="store_true",
                        help="Exit non-zero if the committed SBOM is out of date "
                             "(ignores the creation timestamp).")
    args = parser.parse_args()

    with open(DATA_FILE, encoding="utf-8") as fh:
        data = json.load(fh)

    version = read_version()
    out_path = os.path.join(args.output_dir, "lvgl-%s.spdx.json" % version)

    if args.date:
        created = "%sT00:00:00Z" % args.date
    else:
        created = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    document = build_document(data, version, args.spec_version, created)
    components = [c for c in data["components"] if c.get("third_party", True)]

    if args.check:
        if not os.path.isfile(out_path):
            print("%s is missing; run scripts/generate_sbom.py" % out_path)
            return 1
        with open(out_path, encoding="utf-8") as fh:
            current = fh.read()
        if _normalized(json.loads(current)) != _normalized(document):
            print("%s is out of date; run scripts/generate_sbom.py" % out_path)
            return 1
        print("%s is up to date" % out_path)
        return 0

    os.makedirs(args.output_dir, exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as fh:
        fh.write(_serialize(document))

    print("Wrote SPDX %s SBOM for LVGL %s" % (args.spec_version, version))
    print("  %s" % out_path)
    print("  %d third-party components, %d SPDX elements"
          % (len(components), len(document["@graph"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())

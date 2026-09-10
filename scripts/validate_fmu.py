#!/usr/bin/env python3
"""
FMI 2.0 / FMI 3.0 & FMI-LS-BUS FMU Validator.
Validates FMU archive structure, modelDescription.xml, fmi-ls-manifest.xml,
and terminalsAndIcons.xml against official schemas and conventions.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path


def find_schemas_dir(explicit_dir=None):
    if explicit_dir and os.path.isdir(explicit_dir):
        return Path(explicit_dir).resolve()
    
    # 1. Look in repo root/schemas
    script_dir = Path(__file__).resolve().parent
    repo_schemas = script_dir.parent / "schemas"
    if repo_schemas.is_dir():
        return repo_schemas
    
    # 2. Look in ../docs
    docs_fmi3 = script_dir.parent.parent / "docs" / "fmi3" / "schema"
    if docs_fmi3.is_dir():
        return script_dir.parent.parent / "docs"
    
    return None


def validate_with_xmllint(xml_path, xsd_path, catalog_path=None):
    xmllint = shutil.which("xmllint")
    if not xmllint:
        return False, "xmllint not installed"
    
    env = os.environ.copy()
    cmd = [xmllint, "--noout"]
    if catalog_path and os.path.exists(catalog_path):
        cmd.extend(["--catalogs"])
        env["XML_CATALOG_FILES"] = str(catalog_path)
    cmd.extend(["--schema", str(xsd_path), str(xml_path)])
    
    result = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode == 0:
        return True, "Valid"
    return False, result.stderr.strip()


def validate_with_lxml(xml_path, xsd_path, catalog_path=None):
    try:
        from lxml import etree
    except ImportError:
        return False, "lxml not installed"
    
    try:
        with open(xsd_path, 'rb') as f_schema:
            schema_root = etree.XML(f_schema.read())
            schema = etree.XMLSchema(schema_root)
        
        with open(xml_path, 'rb') as f_xml:
            doc = etree.parse(f_xml)
        
        schema.assertValid(doc)
        return True, "Valid"
    except Exception as e:
        return False, str(e)


def validate_xml_schema(xml_path, xsd_path, catalog_path=None):
    """Try xmllint first (handles OASIS XML catalogs natively), then lxml."""
    if shutil.which("xmllint"):
        success, msg = validate_with_xmllint(xml_path, xsd_path, catalog_path)
        if success or "not installed" not in msg:
            return success, msg
    
    success, msg = validate_with_lxml(xml_path, xsd_path, catalog_path)
    return success, msg


def validate_fmi3_model_description_structure(xml_path):
    """Semantic and structural checks for FMI 3.0 modelDescription.xml."""
    tree = ET.parse(xml_path)
    root = tree.getroot()
    if root.tag != "fmiModelDescription":
        return False, f"Root element is <{root.tag}>, expected <fmiModelDescription>"
    
    fmi_version = root.attrib.get("fmiVersion", "")
    if not fmi_version.startswith("3."):
        return False, f"Expected fmiVersion starting with 3., got '{fmi_version}'"
    
    for req_attr in ["modelName", "instantiationToken"]:
        if req_attr not in root.attrib:
            return False, f"Missing required attribute '{req_attr}' on <fmiModelDescription>"
    
    # Check for at least one interface type
    has_interface = any(elem.tag in ("CoSimulation", "ModelExchange", "ScheduledExecution") for elem in root)
    if not has_interface:
        return False, "No interface element (<CoSimulation>, <ModelExchange>, <ScheduledExecution>) found"
    
    return True, "Structural validation passed"


def validate_fmi2_model_description_structure(xml_path):
    """Semantic and structural checks for FMI 2.0 modelDescription.xml."""
    tree = ET.parse(xml_path)
    root = tree.getroot()
    if root.tag != "fmiModelDescription":
        return False, f"Root element is <{root.tag}>, expected <fmiModelDescription>"
    
    fmi_version = root.attrib.get("fmiVersion", "")
    if fmi_version != "2.0":
        return False, f"Expected fmiVersion='2.0', got '{fmi_version}'"
    
    for req_attr in ["modelName", "guid"]:
        if req_attr not in root.attrib:
            return False, f"Missing required attribute '{req_attr}' on <fmiModelDescription>"
    
    return True, "Structural validation passed"


def validate_fmu_archive(fmu_path, schemas_dir=None, verbose=False):
    errors = []
    warnings = []
    
    fmu_path = Path(fmu_path).resolve()
    if not fmu_path.exists():
        return False, [f"File not found: {fmu_path}"], []
    
    if not zipfile.is_zipfile(fmu_path):
        return False, [f"Not a valid ZIP archive: {fmu_path}"], []
    
    catalog_path = schemas_dir / "catalog.xml" if schemas_dir else None
    
    with zipfile.ZipFile(fmu_path, 'r') as z:
        namelist = z.namelist()
        
        # 1. Check for modelDescription.xml
        if "modelDescription.xml" not in namelist:
            errors.append("Archive missing 'modelDescription.xml' at root")
            return False, errors, warnings
        
        with tempfile.TemporaryDirectory() as tmpdir:
            tmpdir_path = Path(tmpdir)
            md_path = tmpdir_path / "modelDescription.xml"
            z.extract("modelDescription.xml", path=tmpdir_path)
            
            # Read fmiVersion
            try:
                tree = ET.parse(md_path)
                root = tree.getroot()
                fmi_version = root.attrib.get("fmiVersion", "")
            except Exception as e:
                errors.append(f"Failed to parse 'modelDescription.xml': {e}")
                return False, errors, warnings
            
            # Determine FMI generation
            is_fmi3 = fmi_version.startswith("3.")
            is_fmi2 = fmi_version.startswith("2.")
            
            if not is_fmi3 and not is_fmi2:
                errors.append(f"Unsupported fmiVersion: '{fmi_version}'")
                return False, errors, warnings
            
            # 2. Schema validation for modelDescription.xml
            if is_fmi3:
                struct_ok, struct_msg = validate_fmi3_model_description_structure(md_path)
                if not struct_ok:
                    errors.append(f"FMI 3 structural error: {struct_msg}")
                
                if schemas_dir:
                    xsd_fmi3 = schemas_dir / "fmi3" / "fmi3ModelDescription.xsd"
                    if not xsd_fmi3.exists():
                        xsd_fmi3 = schemas_dir / "schema" / "fmi3ModelDescription.xsd"
                    if xsd_fmi3.exists():
                        ok, msg = validate_xml_schema(md_path, xsd_fmi3, catalog_path)
                        if not ok:
                            errors.append(f"FMI 3.0 XSD validation failed: {msg}")
                        elif verbose:
                            print(f"  [OK] modelDescription.xml satisfies FMI 3 schema")
                    else:
                        warnings.append("FMI 3 XSD schema not found for validation")
            elif is_fmi2:
                struct_ok, struct_msg = validate_fmi2_model_description_structure(md_path)
                if not struct_ok:
                    errors.append(f"FMI 2 structural error: {struct_msg}")
                
                if schemas_dir:
                    xsd_fmi2 = schemas_dir / "fmi2" / "fmi2ModelDescription.xsd"
                    if xsd_fmi2.exists():
                        ok, msg = validate_xml_schema(md_path, xsd_fmi2, catalog_path)
                        if not ok:
                            errors.append(f"FMI 2.0 XSD validation failed: {msg}")
                        elif verbose:
                            print(f"  [OK] modelDescription.xml satisfies FMI 2 schema")
                    else:
                        warnings.append("FMI 2 XSD schema not found for validation")
            
            # 3. Binaries check
            binaries_entries = [n for n in namelist if n.startswith("binaries/")]
            if not binaries_entries:
                warnings.append("No 'binaries/' found in FMU (source-only FMU?)")
            else:
                if is_fmi3:
                    # Expect binaries/<arch>-<os>/<model>.<ext>
                    fmi3_bin_dirs = [n for n in binaries_entries if len(n.split('/')) >= 3]
                    if not fmi3_bin_dirs:
                        warnings.append("No architecture-specific directory under 'binaries/' for FMI 3")
                elif is_fmi2:
                    # Expect binaries/<platform>/
                    fmi2_bin_dirs = [n for n in binaries_entries if len(n.split('/')) >= 3]
                    if not fmi2_bin_dirs:
                        warnings.append("No platform directory under 'binaries/' for FMI 2")
            
            # 4. Check for LS-BUS manifest if present
            manifest_entry = "extra/org.fmi-standard.fmi-ls-bus/fmi-ls-manifest.xml"
            if manifest_entry in namelist:
                z.extract(manifest_entry, path=tmpdir_path)
                man_path = tmpdir_path / manifest_entry
                if schemas_dir:
                    xsd_ls_bus = schemas_dir / "fmi-ls-bus" / "fmi3LayeredStandardBusManifest.xsd"
                    if not xsd_ls_bus.exists():
                        xsd_ls_bus = schemas_dir.parent / "docs" / "fmi-ls-bus" / "schema" / "fmi3LayeredStandardBusManifest.xsd"
                    if xsd_ls_bus.exists():
                        ok, msg = validate_xml_schema(man_path, xsd_ls_bus, catalog_path)
                        if not ok:
                            errors.append(f"FMI-LS-BUS manifest validation failed: {msg}")
                        elif verbose:
                            print(f"  [OK] fmi-ls-manifest.xml satisfies LS-BUS schema")
            
            # 5. Check for terminalsAndIcons.xml if present
            terminals_entry = "icons/terminalsAndIcons.xml"
            if terminals_entry in namelist:
                z.extract(terminals_entry, path=tmpdir_path)
                term_path = tmpdir_path / terminals_entry
                if schemas_dir:
                    xsd_term = schemas_dir / "fmi3" / "fmi3TerminalsAndIcons.xsd"
                    if xsd_term.exists():
                        ok, msg = validate_xml_schema(term_path, xsd_term, catalog_path)
                        if not ok:
                            errors.append(f"terminalsAndIcons.xml validation failed: {msg}")
                        elif verbose:
                            print(f"  [OK] terminalsAndIcons.xml satisfies FMI 3 Terminals schema")
    
    return len(errors) == 0, errors, warnings


def main():
    parser = argparse.ArgumentParser(description="Validate FMU archives against FMI/LS-BUS schemas.")
    parser.add_argument("fmus", nargs="*", help="Path(s) to .fmu file(s)")
    parser.add_argument("--dir", help="Directory to search for .fmu files recursively")
    parser.add_argument("--schema-dir", help="Directory containing schema folders (fmi3, fmi2, fmi-ls-bus)")
    parser.add_argument("--require-fmus", action="store_true", help="Fail if no FMUs are found")
    parser.add_argument("--verbose", "-v", action="store_true", help="Print verbose details")
    args = parser.parse_args()
    
    schemas_dir = find_schemas_dir(args.schema_dir)
    if not schemas_dir:
        print("[WARNING] Could not locate schemas directory. Only structural validation will run.", file=sys.stderr)
    elif args.verbose:
        print(f"[INFO] Using schemas directory: {schemas_dir}")
    
    fmu_list = [Path(p) for p in args.fmus]
    if args.dir:
        search_dir = Path(args.dir)
        if search_dir.is_dir():
            fmu_list.extend(search_dir.rglob("*.fmu"))
    
    # Remove duplicates
    unique_fmus = []
    seen = set()
    for f in fmu_list:
        resolved = f.resolve()
        if resolved not in seen:
            seen.add(resolved)
            unique_fmus.append(resolved)
    
    if not unique_fmus:
        if args.require_fmus:
            print("[ERROR] No .fmu files found to validate!", file=sys.stderr)
            sys.exit(1)
        else:
            print("[INFO] No .fmu files specified or found.")
            sys.exit(0)
    
    total = len(unique_fmus)
    passed = 0
    failed = 0
    
    print(f"Validating {total} FMU archive(s)...")
    for fmu in unique_fmus:
        print(f"\n--> Validating: {fmu.name} ({fmu})")
        ok, errors, warnings = validate_fmu_archive(fmu, schemas_dir=schemas_dir, verbose=args.verbose)
        for w in warnings:
            print(f"  [WARN] {w}")
        if ok:
            print(f"  [PASS] {fmu.name}")
            passed += 1
        else:
            print(f"  [FAIL] {fmu.name}")
            for e in errors:
                print(f"    ERROR: {e}")
            failed += 1
    
    print(f"\nSummary: {passed} passed, {failed} failed out of {total} FMU(s).")
    if failed > 0:
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()

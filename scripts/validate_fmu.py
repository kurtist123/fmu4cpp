#!/usr/bin/env python3
"""
FMI 2.0 / FMI 3.0 & FMI-LS-BUS FMU Validator.
Validates FMU archive structure, modelDescription.xml, fmi-ls-manifest.xml,
and terminalsAndIcons.xml against official schemas, semantic rules, and binary conventions.
"""

import argparse
import ctypes
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path


def find_schemas_dir(explicit_dir=None):
    if explicit_dir:
        p = Path(explicit_dir).resolve()
        return p if p.is_dir() else None

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


def get_host_platform_dirs(is_fmi3=True):
    """Return standard platform subdirectory names for the current host system."""
    system = platform.system().lower()
    machine = platform.machine().lower()

    if is_fmi3:
        if system == "linux":
            if machine in ("x86_64", "amd64"):
                return ["x86_64-linux"]
            elif machine in ("aarch64", "arm64"):
                return ["aarch64-linux"]
            elif machine in ("i386", "i686", "x86"):
                return ["x86-linux", "i686-linux"]
        elif system == "windows":
            if machine in ("x86_64", "amd64"):
                return ["x86_64-windows", "x64-windows"]
            elif machine in ("x86", "i386", "i686"):
                return ["x86-windows", "win32-windows"]
            elif machine in ("arm64", "aarch64"):
                return ["arm64-windows"]
        elif system == "darwin":
            if machine in ("x86_64", "amd64"):
                return ["x86_64-darwin"]
            elif machine in ("arm64", "aarch64"):
                return ["aarch64-darwin", "arm64-darwin"]
        return [f"{machine}-{system}"]
    else:
        if system == "linux":
            return (
                ["linux64"]
                if machine in ("x86_64", "amd64", "aarch64")
                else ["linux32"]
            )
        elif system == "windows":
            return ["win64"] if machine in ("x86_64", "amd64", "arm64") else ["win32"]
        elif system == "darwin":
            return (
                ["darwin64"]
                if machine in ("x86_64", "amd64", "arm64")
                else ["darwin32"]
            )
        return [system]


def get_shared_library_extensions():
    """Return valid shared library extensions for the current host system."""
    system = platform.system().lower()
    if system == "windows":
        return [".dll"]
    elif system == "darwin":
        return [".dylib", ".so"]
    else:
        return [".so"]


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

    result = subprocess.run(
        cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )
    if result.returncode == 0:
        return True, "Valid"
    return False, result.stderr.strip()


def validate_with_lxml(xml_path, xsd_path, catalog_path=None):
    try:
        from lxml import etree
    except ImportError:
        return False, "lxml not installed"

    try:
        if catalog_path and os.path.exists(catalog_path):
            os.environ["XML_CATALOG_FILES"] = str(catalog_path)

        # Parse schema from file path to preserve base URL for relative xs:include / xs:import
        schema_doc = etree.parse(str(xsd_path))
        schema = etree.XMLSchema(schema_doc)

        doc = etree.parse(str(xml_path))
        schema.assertValid(doc)
        return True, "Valid"
    except Exception as e:
        return False, str(e)


def validate_xml_schema(xml_path, xsd_path, catalog_path=None, backend="auto"):
    """
    Validate XML against XSD schema.
    Supported backends: 'auto' (xmllint fallback to lxml), 'xmllint', 'lxml', 'both'.
    """
    if backend == "xmllint":
        return validate_with_xmllint(xml_path, xsd_path, catalog_path)
    elif backend == "lxml":
        return validate_with_lxml(xml_path, xsd_path, catalog_path)
    elif backend == "both":
        ok1, msg1 = validate_with_xmllint(xml_path, xsd_path, catalog_path)
        if not ok1:
            return False, f"xmllint failed: {msg1}"
        ok2, msg2 = validate_with_lxml(xml_path, xsd_path, catalog_path)
        if not ok2:
            return False, f"lxml failed: {msg2}"
        return True, "Valid (both xmllint and lxml passed)"
    else:  # auto
        if shutil.which("xmllint"):
            success, msg = validate_with_xmllint(xml_path, xsd_path, catalog_path)
            if success or "not installed" not in msg:
                return success, msg
        return validate_with_lxml(xml_path, xsd_path, catalog_path)


def validate_fmi3_model_description_structure(xml_path):
    """Structural checks for FMI 3.0 modelDescription.xml."""
    tree = ET.parse(xml_path)
    root = tree.getroot()
    if root.tag != "fmiModelDescription":
        return False, f"Root element is <{root.tag}>, expected <fmiModelDescription>"

    fmi_version = root.attrib.get("fmiVersion", "")
    if not fmi_version.startswith("3."):
        return False, f"Expected fmiVersion starting with 3., got '{fmi_version}'"

    for req_attr in ["modelName", "instantiationToken"]:
        if req_attr not in root.attrib:
            return (
                False,
                f"Missing required attribute '{req_attr}' on <fmiModelDescription>",
            )

    has_interface = any(
        elem.tag in ("CoSimulation", "ModelExchange", "ScheduledExecution")
        for elem in root
    )
    if not has_interface:
        return (
            False,
            "No interface element (<CoSimulation>, <ModelExchange>, <ScheduledExecution>) found",
        )

    return True, "Structural validation passed"


def validate_fmi2_model_description_structure(xml_path):
    """Structural checks for FMI 2.0 modelDescription.xml."""
    tree = ET.parse(xml_path)
    root = tree.getroot()
    if root.tag != "fmiModelDescription":
        return False, f"Root element is <{root.tag}>, expected <fmiModelDescription>"

    fmi_version = root.attrib.get("fmiVersion", "")
    if fmi_version != "2.0":
        return False, f"Expected fmiVersion='2.0', got '{fmi_version}'"

    for req_attr in ["modelName", "guid"]:
        if req_attr not in root.attrib:
            return (
                False,
                f"Missing required attribute '{req_attr}' on <fmiModelDescription>",
            )

    has_interface = any(elem.tag in ("CoSimulation", "ModelExchange") for elem in root)
    if not has_interface:
        return False, "No interface element (<CoSimulation>, <ModelExchange>) found"

    return True, "Structural validation passed"


def validate_fmi_model_description_semantics(xml_path, is_fmi3=True):
    """Semantic conformance checks on modelDescription.xml."""
    errors = []
    warnings = []

    try:
        tree = ET.parse(xml_path)
        root = tree.getroot()
    except Exception as e:
        return [f"XML parse error in semantic validation: {e}"], []

    # Check interface elements & capability spellings
    interface_tags = (
        ("CoSimulation", "ModelExchange", "ScheduledExecution")
        if is_fmi3
        else ("CoSimulation", "ModelExchange")
    )
    found_interfaces = [elem for elem in root if elem.tag in interface_tags]
    if not found_interfaces:
        errors.append("No interface element (<CoSimulation>, <ModelExchange>) found")

    for iface in found_interfaces:
        model_id = iface.attrib.get("modelIdentifier")
        if not model_id:
            errors.append(
                f"Interface <{iface.tag}> missing required attribute 'modelIdentifier'"
            )
        elif not re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", model_id):
            errors.append(f"modelIdentifier '{model_id}' is not a valid C identifier")

        if is_fmi3:
            for attr in iface.attrib:
                if attr == "canGetAndSetFMUstate":
                    warnings.append(
                        f"FMI 3 <{iface.tag}> attribute 'canGetAndSetFMUstate' uses FMI 2 casing; standard FMI 3 requires 'canGetAndSetFMUState'"
                    )
                elif attr == "canSerializeFMUstate":
                    warnings.append(
                        f"FMI 3 <{iface.tag}> attribute 'canSerializeFMUstate' uses FMI 2 casing; standard FMI 3 requires 'canSerializeFMUState'"
                    )

    # Check ModelVariables
    mv = root.find("ModelVariables")
    if mv is None:
        errors.append("Missing required <ModelVariables> element")
        return errors, warnings

    seen_names = set()
    vr_to_var = {}
    variables_list = list(mv)

    for idx, var in enumerate(variables_list, start=1):
        name = var.attrib.get("name")
        if not name:
            errors.append(f"Variable at index {idx} missing 'name' attribute")
        elif name in seen_names:
            errors.append(f"Duplicate variable name '{name}' in ModelVariables")
        else:
            seen_names.add(name)

        vr_str = var.attrib.get("valueReference")
        if vr_str is None:
            errors.append(f"Variable '{name}' missing 'valueReference'")
        else:
            try:
                vr = int(vr_str)
                if vr < 0:
                    errors.append(f"Variable '{name}' has negative valueReference {vr}")
                vr_to_var[vr_str] = var
            except ValueError:
                errors.append(
                    f"Variable '{name}' valueReference '{vr_str}' is not an integer"
                )

        causality = var.attrib.get("causality", "local")
        variability = var.attrib.get("variability")

        if causality in ("parameter", "structuralParameter"):
            if variability and variability not in ("fixed", "tunable"):
                errors.append(
                    f"Parameter variable '{name}' cannot have variability='{variability}'"
                )
        elif causality == "independent":
            if variability and variability != "continuous":
                errors.append(
                    f"Independent variable '{name}' must have variability='continuous'"
                )
            if is_fmi3 and var.tag not in ("Float64", "Real"):
                errors.append(f"Independent variable '{name}' must be Float64")

        if is_fmi3 and var.tag == "Clock":
            interval_var = var.attrib.get("intervalVariability", "triggered")
            if interval_var not in (
                "triggered",
                "countdown",
                "continuous",
                "fixed",
                "changing",
                "tunable",
                "constant",
            ):
                errors.append(
                    f"Clock '{name}' has invalid intervalVariability='{interval_var}'"
                )

    # Check ModelStructure references
    ms = root.find("ModelStructure")
    if ms is not None:
        if is_fmi3:
            for child in ms:
                vr_ref = child.attrib.get("valueReference")
                if vr_ref and vr_ref not in vr_to_var:
                    errors.append(
                        f"<ModelStructure> element <{child.tag}> references unknown valueReference '{vr_ref}'"
                    )
                deps = child.attrib.get("dependencies", "").split()
                for d in deps:
                    if d not in vr_to_var:
                        errors.append(
                            f"<ModelStructure> element <{child.tag}> dependency references unknown valueReference '{d}'"
                        )
        else:
            for group in ms:
                for unk in group:
                    idx_str = unk.attrib.get("index")
                    if idx_str:
                        try:
                            idx_val = int(idx_str)
                            if idx_val < 1 or idx_val > len(variables_list):
                                errors.append(
                                    f"FMI 2 <ModelStructure> index '{idx_str}' out of range [1, {len(variables_list)}]"
                                )
                        except ValueError:
                            errors.append(
                                f"FMI 2 <ModelStructure> invalid index '{idx_str}'"
                            )

    # Check UnitDefinitions references
    unit_defs = root.find("UnitDefinitions")
    defined_units = set()
    if unit_defs is not None:
        for u in unit_defs.findall("Unit"):
            uname = u.attrib.get("name")
            if uname:
                defined_units.add(uname)

    for var in variables_list:
        v_unit = var.attrib.get("unit")
        if v_unit and v_unit not in defined_units:
            warnings.append(
                f"Variable '{var.attrib.get('name')}' references unit '{v_unit}' not defined in <UnitDefinitions>"
            )

    return errors, warnings


def check_binary_symbols(binary_path, is_fmi3, is_cosimulation=True):
    """Dynamically load binary and verify required C ABI symbols."""
    try:
        lib = ctypes.CDLL(str(binary_path))
    except Exception as e:
        return False, [f"Failed to dynamically load '{binary_path.name}': {e}"]

    missing_symbols = []
    if is_fmi3:
        required_symbols = [
            "fmi3GetVersion",
            "fmi3FreeInstance",
            "fmi3EnterInitializationMode",
            "fmi3ExitInitializationMode",
            "fmi3Terminate",
            "fmi3Reset",
            "fmi3GetFloat64",
            "fmi3SetFloat64",
            "fmi3GetBoolean",
            "fmi3SetBoolean",
            "fmi3GetString",
            "fmi3SetString",
            "fmi3GetBinary",
            "fmi3SetBinary",
        ]
        if is_cosimulation:
            required_symbols.extend(
                [
                    "fmi3InstantiateCoSimulation",
                    "fmi3EnterStepMode",
                    "fmi3DoStep",
                ]
            )
        else:
            required_symbols.extend(
                [
                    "fmi3InstantiateModelExchange",
                    "fmi3EnterContinuousTimeMode",
                    "fmi3CompletedIntegratorStep",
                ]
            )
    else:
        required_symbols = [
            "fmi2GetTypesPlatform",
            "fmi2GetVersion",
            "fmi2Instantiate",
            "fmi2FreeInstance",
            "fmi2SetupExperiment",
            "fmi2EnterInitializationMode",
            "fmi2ExitInitializationMode",
            "fmi2Terminate",
            "fmi2Reset",
            "fmi2GetReal",
            "fmi2GetInteger",
            "fmi2GetBoolean",
            "fmi2GetString",
            "fmi2SetReal",
            "fmi2SetInteger",
            "fmi2SetBoolean",
            "fmi2SetString",
        ]
        if is_cosimulation:
            required_symbols.append("fmi2DoStep")

    for sym in required_symbols:
        if not hasattr(lib, sym):
            missing_symbols.append(sym)

    if missing_symbols:
        return False, [
            f"Missing required FMI symbol(s) in {binary_path.name}: {', '.join(missing_symbols)}"
        ]
    return True, []


def validate_fmu_archive(
    fmu_path,
    schemas_dir=None,
    backend="auto",
    strict=False,
    check_symbols=False,
    profile="binary",
    verbose=False,
):
    errors = []
    warnings = []
    info = {"modelName": "", "generation": "", "fmiVersion": ""}

    fmu_path = Path(fmu_path).resolve()
    if not fmu_path.exists():
        return False, [f"File not found: {fmu_path}"], [], info

    if not zipfile.is_zipfile(fmu_path):
        return False, [f"Not a valid ZIP archive: {fmu_path}"], [], info

    catalog_path = schemas_dir / "catalog.xml" if schemas_dir else None

    with zipfile.ZipFile(fmu_path, "r") as z:
        namelist = z.namelist()

        # 1. Check for modelDescription.xml
        if "modelDescription.xml" not in namelist:
            errors.append("Archive missing 'modelDescription.xml' at root")
            return False, errors, warnings, info

        with tempfile.TemporaryDirectory() as tmpdir:
            tmpdir_path = Path(tmpdir)
            md_path = tmpdir_path / "modelDescription.xml"
            z.extract("modelDescription.xml", path=tmpdir_path)

            # Read metadata
            try:
                tree = ET.parse(md_path)
                root = tree.getroot()
                fmi_version = root.attrib.get("fmiVersion", "")
                model_name = root.attrib.get("modelName", "")
                info["modelName"] = model_name
                info["fmiVersion"] = fmi_version
            except Exception as e:
                errors.append(f"Failed to parse 'modelDescription.xml': {e}")
                return False, errors, warnings, info

            is_fmi3 = fmi_version.startswith("3.")
            is_fmi2 = fmi_version.startswith("2.")
            info["generation"] = (
                "fmi3" if is_fmi3 else ("fmi2" if is_fmi2 else "unknown")
            )

            if not is_fmi3 and not is_fmi2:
                errors.append(f"Unsupported fmiVersion: '{fmi_version}'")
                return False, errors, warnings, info

            # 2. Structural & Semantic validation for modelDescription.xml
            if is_fmi3:
                struct_ok, struct_msg = validate_fmi3_model_description_structure(
                    md_path
                )
                if not struct_ok:
                    errors.append(f"FMI 3 structural error: {struct_msg}")

                if schemas_dir:
                    xsd_fmi3 = schemas_dir / "fmi3" / "fmi3ModelDescription.xsd"
                    if not xsd_fmi3.exists():
                        xsd_fmi3 = schemas_dir / "schema" / "fmi3ModelDescription.xsd"
                    if xsd_fmi3.exists():
                        ok, msg = validate_xml_schema(
                            md_path, xsd_fmi3, catalog_path, backend=backend
                        )
                        if not ok:
                            errors.append(f"FMI 3.0 XSD validation failed: {msg}")
                        elif verbose:
                            print(
                                f"  [OK] modelDescription.xml satisfies FMI 3 schema ({backend})"
                            )
                    else:
                        msg = "FMI 3 XSD schema not found for validation"
                        if strict:
                            errors.append(msg)
                        else:
                            warnings.append(msg)
                elif strict:
                    errors.append(
                        "Schema directory not found; XSD validation required in strict mode"
                    )
            elif is_fmi2:
                struct_ok, struct_msg = validate_fmi2_model_description_structure(
                    md_path
                )
                if not struct_ok:
                    errors.append(f"FMI 2 structural error: {struct_msg}")

                if schemas_dir:
                    xsd_fmi2 = schemas_dir / "fmi2" / "fmi2ModelDescription.xsd"
                    if xsd_fmi2.exists():
                        ok, msg = validate_xml_schema(
                            md_path, xsd_fmi2, catalog_path, backend=backend
                        )
                        if not ok:
                            errors.append(f"FMI 2.0 XSD validation failed: {msg}")
                        elif verbose:
                            print(
                                f"  [OK] modelDescription.xml satisfies FMI 2 schema ({backend})"
                            )
                    else:
                        msg = "FMI 2 XSD schema not found for validation"
                        if strict:
                            errors.append(msg)
                        else:
                            warnings.append(msg)
                elif strict:
                    errors.append(
                        "Schema directory not found; XSD validation required in strict mode"
                    )

            # Semantic conformance check
            sem_errors, sem_warnings = validate_fmi_model_description_semantics(
                md_path, is_fmi3=is_fmi3
            )
            errors.extend(sem_errors)
            warnings.extend(sem_warnings)

            # 3. Binaries check
            iface_elem = None
            for tag in ("CoSimulation", "ModelExchange", "ScheduledExecution"):
                elem = root.find(tag)
                if elem is not None:
                    iface_elem = elem
                    break
            model_id = (
                iface_elem.attrib.get("modelIdentifier")
                if iface_elem is not None
                else None
            )

            binaries_entries = [n for n in namelist if n.startswith("binaries/")]
            if profile == "binary":
                if not binaries_entries:
                    if strict:
                        errors.append("No 'binaries/' found in FMU")
                    else:
                        warnings.append(
                            "No 'binaries/' found in FMU (source-only FMU?)"
                        )
                else:
                    host_dirs = get_host_platform_dirs(is_fmi3=is_fmi3)
                    lib_exts = get_shared_library_extensions()

                    host_bin_found = False
                    host_bin_entry = None
                    if model_id:
                        for hdir in host_dirs:
                            for ext in lib_exts:
                                cand = f"binaries/{hdir}/{model_id}{ext}"
                                if cand in namelist:
                                    host_bin_found = True
                                    host_bin_entry = cand
                                    break
                            if host_bin_found:
                                break

                    if not host_bin_found:
                        matching_dir_files = [
                            n
                            for n in binaries_entries
                            if any(
                                n.startswith(f"binaries/{hdir}/") for hdir in host_dirs
                            )
                        ]
                        if matching_dir_files:
                            errors.append(
                                f"Host architecture directory present, but missing expected binary '{model_id}' ({matching_dir_files})"
                            )
                        elif strict:
                            errors.append(
                                f"Missing expected host binary: binaries/{host_dirs[0]}/{model_id}{lib_exts[0]}"
                            )
                        else:
                            warnings.append(
                                f"No binary found for current host platform ({host_dirs[0]})"
                            )
                    else:
                        if verbose:
                            print(
                                f"  [OK] Found matching host binary: {host_bin_entry}"
                            )

                        # Dynamic load & symbol verification
                        if check_symbols or strict:
                            z.extract(host_bin_entry, path=tmpdir_path)
                            extracted_bin = tmpdir_path / host_bin_entry
                            is_cs = root.find("CoSimulation") is not None
                            sym_ok, sym_errs = check_binary_symbols(
                                extracted_bin, is_fmi3=is_fmi3, is_cosimulation=is_cs
                            )
                            if not sym_ok:
                                errors.extend(sym_errs)
                            elif verbose:
                                print(
                                    f"  [OK] Verified dynamic loading and exported symbols in {host_bin_entry}"
                                )

            # 4. Sources check
            sources_entries = [n for n in namelist if n.startswith("sources/")]
            if profile == "source-only" and not sources_entries:
                errors.append(
                    "Profile 'source-only' requested, but no 'sources/' directory found in archive"
                )
            elif sources_entries:
                bd_entry = "sources/buildDescription.xml"
                if bd_entry in namelist:
                    z.extract(bd_entry, path=tmpdir_path)
                    bd_path = tmpdir_path / bd_entry
                    if schemas_dir:
                        xsd_bd = schemas_dir / "fmi3" / "fmi3BuildDescription.xsd"
                        if xsd_bd.exists():
                            ok, msg = validate_xml_schema(
                                bd_path, xsd_bd, catalog_path, backend=backend
                            )
                            if not ok:
                                errors.append(
                                    f"sources/buildDescription.xml validation failed: {msg}"
                                )
                            elif verbose:
                                print(
                                    f"  [OK] sources/buildDescription.xml satisfies FMI 3 BuildDescription schema"
                                )

            # 5. Check for LS-BUS manifest if present
            manifest_entry = "extra/org.fmi-standard.fmi-ls-bus/fmi-ls-manifest.xml"
            if manifest_entry in namelist:
                z.extract(manifest_entry, path=tmpdir_path)
                man_path = tmpdir_path / manifest_entry
                if schemas_dir:
                    xsd_ls_bus = (
                        schemas_dir
                        / "fmi-ls-bus"
                        / "fmi3LayeredStandardBusManifest.xsd"
                    )
                    if not xsd_ls_bus.exists():
                        xsd_ls_bus = (
                            schemas_dir.parent
                            / "docs"
                            / "fmi-ls-bus"
                            / "schema"
                            / "fmi3LayeredStandardBusManifest.xsd"
                        )
                    if xsd_ls_bus.exists():
                        ok, msg = validate_xml_schema(
                            man_path, xsd_ls_bus, catalog_path, backend=backend
                        )
                        if not ok:
                            errors.append(
                                f"FMI-LS-BUS manifest validation failed: {msg}"
                            )
                        elif verbose:
                            print(f"  [OK] fmi-ls-manifest.xml satisfies LS-BUS schema")

            # 6. Check for terminalsAndIcons.xml if present
            terminals_entry = "icons/terminalsAndIcons.xml"
            if terminals_entry in namelist:
                z.extract(terminals_entry, path=tmpdir_path)
                term_path = tmpdir_path / terminals_entry
                if schemas_dir:
                    xsd_term = schemas_dir / "fmi3" / "fmi3TerminalsAndIcons.xsd"
                    if xsd_term.exists():
                        ok, msg = validate_xml_schema(
                            term_path, xsd_term, catalog_path, backend=backend
                        )
                        if not ok:
                            errors.append(
                                f"terminalsAndIcons.xml validation failed: {msg}"
                            )
                        elif verbose:
                            print(
                                f"  [OK] terminalsAndIcons.xml satisfies FMI 3 Terminals schema"
                            )

    return len(errors) == 0, errors, warnings, info


def main():
    parser = argparse.ArgumentParser(
        description="Validate FMU archives against FMI/LS-BUS schemas and conventions."
    )
    parser.add_argument("fmus", nargs="*", help="Path(s) to .fmu file(s)")
    parser.add_argument("--dir", help="Directory to search for .fmu files recursively")
    parser.add_argument(
        "--schema-dir",
        help="Directory containing schema folders (fmi3, fmi2, fmi-ls-bus)",
    )
    parser.add_argument(
        "--backend",
        choices=["auto", "xmllint", "lxml", "both"],
        default="auto",
        help="Schema validation backend to use (default: auto)",
    )
    parser.add_argument(
        "--strict",
        "--conformance",
        action="store_true",
        help="Strict conformance mode: fail on missing schemas or binaries",
    )
    parser.add_argument(
        "--check-symbols",
        action="store_true",
        help="Dynamically load host binary and verify required exported FMI symbols",
    )
    parser.add_argument(
        "--require-fmus", action="store_true", help="Fail if no FMUs are found"
    )
    parser.add_argument(
        "--expect-models",
        nargs="+",
        help="Expected model names (fail if any are missing)",
    )
    parser.add_argument(
        "--expect-generations",
        nargs="+",
        help="Expected FMI generations, e.g. fmi2 fmi3 (fail if missing)",
    )
    parser.add_argument(
        "--profile",
        choices=["binary", "source-only", "all"],
        default="binary",
        help="Validation artifact profile (default: binary)",
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true", help="Print verbose details"
    )
    args = parser.parse_args()

    schemas_dir = find_schemas_dir(args.schema_dir)
    if not schemas_dir:
        if args.strict:
            print(
                "[ERROR] Could not locate schemas directory in strict mode!",
                file=sys.stderr,
            )
            sys.exit(1)
        else:
            print(
                "[WARNING] Could not locate schemas directory. Only structural validation will run.",
                file=sys.stderr,
            )
    elif args.verbose:
        print(f"[INFO] Using schemas directory: {schemas_dir}")
        print(f"[INFO] Using validation backend: {args.backend}")

    fmu_list = [Path(p) for p in args.fmus]
    if args.dir:
        search_dir = Path(args.dir)
        if search_dir.is_dir():
            fmu_list.extend(search_dir.rglob("*.fmu"))

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
    seen_models = set()
    seen_generations = set()

    print(
        f"Validating {total} FMU archive(s) (backend={args.backend}, strict={args.strict})..."
    )
    for fmu in unique_fmus:
        print(f"\n--> Validating: {fmu.name} ({fmu})")
        ok, errors, warnings, info = validate_fmu_archive(
            fmu,
            schemas_dir=schemas_dir,
            backend=args.backend,
            strict=args.strict,
            check_symbols=args.check_symbols,
            profile=args.profile,
            verbose=args.verbose,
        )
        if info["modelName"]:
            seen_models.add(info["modelName"])
        if info["generation"]:
            seen_generations.add(info["generation"])
            if info["generation"] == "fmi2":
                seen_generations.add("2")
            elif info["generation"] == "fmi3":
                seen_generations.add("3")

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

    # Assert expected models
    if args.expect_models:
        missing_models = set(args.expect_models) - seen_models
        if missing_models:
            print(
                f"\n[ERROR] Missing expected model(s): {', '.join(sorted(missing_models))}",
                file=sys.stderr,
            )
            failed += len(missing_models)

    # Assert expected generations
    if args.expect_generations:
        missing_gens = set(args.expect_generations) - seen_generations
        if missing_gens:
            print(
                f"\n[ERROR] Missing expected generation(s): {', '.join(sorted(missing_gens))}",
                file=sys.stderr,
            )
            failed += len(missing_gens)

    print(f"\nSummary: {passed} passed, {failed} failed out of {total} FMU(s).")
    if failed > 0:
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()

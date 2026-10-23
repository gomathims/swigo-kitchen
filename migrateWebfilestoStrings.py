import os
import mimetypes
import re
import gzip
import json as jsonlib
import hashlib
import shutil
import htmlmin
from csscompressor import compress
from jsmin import jsmin

# Ensure modern image types resolve on older Python installs that may not
# have them in the default mimetypes table.
mimetypes.add_type('image/webp', '.webp')
mimetypes.add_type('image/avif', '.avif')

from SCons.Script import ARGUMENTS, DefaultEnvironment

from datetime import datetime
import glob
import time

# Files larger than this threshold (bytes, after minification) will be gzipped
# and stored as binary arrays instead of C strings
GZIP_THRESHOLD = 4096


# Build numbering: bumps only when source inputs change
BUILD_NUMBER_FILE = "build_number.txt"
BUILD_HASH_FILE = ".build_hash.txt"
ELF_ARCHIVE_DIR = "../supersmartELFs"

SOURCE_PATTERNS = [
    "src/**/*.cpp", "src/**/*.h", "src/**/*.c",
    "data/**/*",
    "platformio.ini", "partitions.csv",
]
# Generated files: excluded so the hash stays stable across builds
EXCLUDED_FROM_HASH = {
    "src/build_info.h",
    "src/web_content.h",
    "src/web_mappings.h",
}

_source_hash_cache = None

def compute_source_hash():
    global _source_hash_cache
    if _source_hash_cache is not None:
        return _source_hash_cache
    h = hashlib.sha256()
    paths = set()
    for pat in SOURCE_PATTERNS:
        paths.update(glob.glob(pat, recursive=True))
    for p in sorted(paths):
        norm = p.replace("\\", "/")
        if not os.path.isfile(p) or norm in EXCLUDED_FROM_HASH:
            continue
        h.update(norm.encode())
        with open(p, "rb") as f:
            h.update(f.read())
    _source_hash_cache = h.hexdigest()
    return _source_hash_cache

def get_build_number():
    current = compute_source_hash()
    saved_num = 0
    saved_hash = ""
    if os.path.exists(BUILD_NUMBER_FILE):
        try:
            with open(BUILD_NUMBER_FILE, "r", encoding="utf-8") as f:
                saved_num = int(f.read().strip())
        except (ValueError, OSError):
            pass
    if os.path.exists(BUILD_HASH_FILE):
        try:
            with open(BUILD_HASH_FILE, "r", encoding="utf-8") as f:
                saved_hash = f.read().strip()
        except OSError:
            pass
    if saved_hash == current and saved_num > 0:
        return saved_num
    return saved_num + 1

def save_build_number(build_number):
    with open(BUILD_NUMBER_FILE, "w", encoding="utf-8") as f:
        f.write(str(build_number))
    with open(BUILD_HASH_FILE, "w", encoding="utf-8") as f:
        f.write(compute_source_hash())

def get_version_build_number():
    build_number = get_build_number()

    # Retrieve build flags
    build_mode = env.ParseFlags(env['BUILD_FLAGS']).get('CPPDEFINES', [])
    build_mode_dict = {key: value for (key, value) in build_mode if isinstance(key, str)}

    # Extract buildMode, major, and minor
    major_version = build_mode_dict.get('FIRMWARE_MAJOR', '0')
    minor_version = build_mode_dict.get('FIRMWARE_MINOR', '0')
    patch_version = build_mode_dict.get('FIRMWARE_PATCH', '0')

    # Define the custom firmware name
    version_str = f"v{major_version}.{minor_version}.{patch_version}_{build_number}"
    return version_str

# Modify the function to accept extra arguments
BUILD_DATE_FILE = ".last_build_date.txt"

def get_stable_build_date():
    """Reuse the last build date if a recent build exists (within 5 minutes).
    This prevents BUILD_DATE changes from triggering a full rebuild on upload."""
    now = datetime.now()
    if os.path.exists(BUILD_DATE_FILE):
        try:
            with open(BUILD_DATE_FILE, "r", encoding="utf-8") as f:
                saved = f.read().strip()
            saved_dt = datetime.strptime(saved, "%Y-%m-%d_%H-%M")
            if (now - saved_dt).total_seconds() < 300:
                return saved
        except (ValueError, OSError):
            pass
    build_date = now.strftime("%Y-%m-%d_%H-%M")
    with open(BUILD_DATE_FILE, "w", encoding="utf-8") as f:
        f.write(build_date)
    return build_date

BUILD_INFO_HEADER = "src/build_info.h"

def write_build_info_header(build_number, build_date):
    """Write BUILD_NUMBER and BUILD_DATE to a header file instead of CPPDEFINES.
    Only files that #include this header will recompile when values change."""
    new_content = (
        "#ifndef BUILD_INFO_H\n"
        "#define BUILD_INFO_H\n"
        f"#define BUILD_NUMBER {build_number}\n"
        f'#define BUILD_DATE "{build_date}"\n'
        "#endif\n"
    )
    # Only write if content changed to avoid unnecessary recompilation
    if os.path.exists(BUILD_INFO_HEADER):
        with open(BUILD_INFO_HEADER, "r", encoding="utf-8") as f:
            if f.read() == new_content:
                return
    with open(BUILD_INFO_HEADER, "w", encoding="utf-8") as f:
        f.write(new_content)

def on_pre_build():

    build_number = get_build_number()
    print(f"Build Number: {build_number}")

    # Reuse recent build date to avoid rebuild on upload
    build_date = get_stable_build_date()

    # Write to header file instead of CPPDEFINES — avoids recompiling all libraries
    write_build_info_header(build_number, build_date)

    # Retrieve build flags
    build_mode = env.ParseFlags(env['BUILD_FLAGS']).get('CPPDEFINES', [])
    build_mode_dict = {key: value for (key, value) in build_mode if isinstance(key, str)}

    # Extract buildMode, major, and minor

    special_password = build_mode_dict.get('SPECIAL_PASSWORD', '0').replace('"', '')
    build_mode_value = "SP" if special_password == "1" else "Production"
    # timing_value = build_mode_dict.get('TIMERCONFIG', '5M').replace('"', '')
    major_version = build_mode_dict.get('FIRMWARE_MAJOR', '0')
    minor_version = build_mode_dict.get('FIRMWARE_MINOR', '0')
    patch_version = build_mode_dict.get('FIRMWARE_PATCH', '0')
    # build_number_str = str(build_number) + "_SP" if special_password == "1" else str(build_number)

    # Define the custom firmware name
    led_normal = build_mode_dict.get('LED_NORMAL', False)
    led_type = "BLUE" if led_normal else "RGB"
    build_type = build_mode_dict.get('PROD_BETA', 'B').replace('"', '')
    build_type_str = "BETA" if build_type == "B" else "Production"
    if build_type == "B":
        build_number_str = "." + str(build_number) + "_SP" if special_password == "1" else "." + str(build_number)
    else:
        build_number_str = "_SP" if special_password == "1" else ""
    firmware_name = f"firmware_v{major_version}.{minor_version}.{patch_version}{build_number_str}_{build_date}_{build_type_str}"

    # Update the output binary name
    env.Replace(PROGNAME=firmware_name)

# Hook into the build system
Import("env")

print(f"Build Number: {BUILD_NUMBER_FILE}")
on_pre_build()


######### For Preparing strings corresponding to html/CSS/script/image files ##################
def minify_inline_css(html):
    """Minify inline CSS in the HTML."""
    def minify_css(match):
        css = match.group(2)  
        return f'<style>{compress(css)}</style>'  

    # Regex to find <style> tags and their content
    return re.sub(r'<style([^>]*>)(.*?)(</style>)', minify_css, html, flags=re.DOTALL)

# def minify_inline_js(html):
#     """Minify inline JavaScript in the HTML."""
#     def minify_js(match):
#         js = match.group(2) 
#         return f'<script{jsmin(js)}</script>'  # Minify and reinsert

#     # Regex to find <script> tags and their content
#     return re.sub(r'(<script[^>]*>)(.*?)(</script>)', minify_js, html, flags=re.DOTALL)

def minify_inline_js(html):
    """Minify inline JavaScript in the HTML."""
    def minify_js(match):
        js = match.group(2)  # Extract JS content
        return f'<script{match.group(1)}{jsmin(js)}</script>'  # Minify and reinsert

    # Regex to find <script> tags and their content
    return re.sub(r'<script([^>]*>)(.*?)(</script>)', minify_js, html, flags=re.DOTALL)
def minify_html(content):
    minified_html = minify_inline_css(content)
    minified_html = minify_inline_js(minified_html)
    return htmlmin.minify(minified_html, remove_comments=True,           # Remove HTML comments
            remove_empty_space=True,        # Remove empty spaces
            remove_all_empty_space=True,    # Aggressive whitespace removal
            reduce_empty_attributes=True,   # Reduce empty attributes
            remove_optional_attribute_quotes=True  # Remove optional quotes
            )

def minify_css(content):
    return compress(content)

def replace_template_literals(js):
    return re.sub(r'`(.*?)`', lambda m: '"' + m.group(1).replace('${', '" + ').replace('}', ' + "') + '"', js, flags=re.DOTALL)


def minify_js(content):
    return jsmin(content)
def minimize_text(content, filename):
    """Minimize HTML, CSS, or JS content by removing unnecessary spaces and newlines."""
    # Remove comments (HTML, CSS, JS)
    version_string = get_version_build_number()
    if filename.endswith('.html') or filename.endswith('.htm'):
        modified = content.replace('##VERSION##', version_string)
        minified = minify_html(modified)
    elif filename.endswith('.css'):
        minified = minify_css(content)
    elif filename.endswith('.js'):
        # preprocessed = replace_template_literals(content)
        # Scripts get the same ##VERSION## substitution as the pages, so a URL
        # built in JS (help.js asking for help_en.json) can be cache-busted the
        # same way a <script src> is.
        minified = minify_js(content.replace('##VERSION##', version_string))
    elif filename.endswith('.json'):
        # Strip whitespace and reject malformed catalogs at build time rather
        # than shipping a file the portal cannot parse. ensure_ascii=False keeps
        # non-ASCII text as UTF-8 instead of bloating it into \uXXXX escapes.
        minified = jsonlib.dumps(jsonlib.loads(content), separators=(',', ':'), ensure_ascii=False)
    elif filename.endswith('.svg'):
        # preprocessed = replace_template_literals(content)
        minified = content
    return minified
    # content = re.sub(r"<!--.*?-->|/\*.*?\*/|//.*?$", "", content, flags=re.DOTALL | re.MULTILINE)
    # # Remove leading and trailing whitespaces and reduce multiple spaces/newlines
    # content = re.sub(r"\s+", " ", content).strip()
    # return content

def convert_text_file(file_path):
    """Convert a text file (HTML, CSS, JS) into a minimized C-style string."""
    print(f" convert_text_file {file_path} ")
    with open(file_path, "r", encoding="utf-8") as f:
        content = f.read()

    # Minimize the content
    minimized_content = minimize_text(content, file_path)

    # Escape any quotes and newlines
    minimized_content = minimized_content.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n\"\n\"")

    # Return the formatted C string
    return f"\"{minimized_content}\""

def convert_binary_file(file_path):
    """Convert a binary file (PNG, ICO) into a C-style byte array."""
    with open(file_path, "rb") as f:
        content = f.read()

    # Format each byte as hex (e.g., 0x1f)
    return ", ".join(f"0x{byte:02x}" for byte in content)

def gzip_content(content_bytes):
    """Gzip compress content and return compressed bytes."""
    return gzip.compress(content_bytes, compresslevel=9)

def convert_to_gzip_array(file_path):
    """Read a text file, minify it, gzip it, and return as C byte array."""
    with open(file_path, "r", encoding="utf-8") as f:
        content = f.read()

    # Minify first
    minimized = minimize_text(content, file_path)
    # Gzip the minified content
    gz_bytes = gzip_content(minimized.encode('utf-8'))

    print(f"  GZIP: {file_path} -> {len(minimized)} minified -> {len(gz_bytes)} gzipped ({100 - len(gz_bytes)*100//len(minimized)}% reduction)")

    hex_array = ", ".join(f"0x{b:02x}" for b in gz_bytes)
    return hex_array, len(gz_bytes)

def process_folder(folder_path, output_file, output_map_file):
    """Process all files in a folder and generate C arrays or strings and the corresponding map."""
    text_map = {}
    binary_map = {}
    gzip_map = {}  # For large text files stored as gzipped binary

    # Iterate over all files in the folder and subfolders
    for root, _, files in os.walk(folder_path):
        for file_name in files:

            file_path = os.path.join(root, file_name)

            # Get relative path (to use as URL)
            relative_path = os.path.relpath(file_path, folder_path).replace("\\", "/")
            mime_type, _ = mimetypes.guess_type(file_path)
            url_path = "/" + relative_path
            print(f"file name {url_path} and {mime_type}")
            # Generate variable name from file path (sanitize special chars)
            variable_name = os.path.splitext(relative_path.replace("/", "_").replace(".", "_"))[0]

            # Text-based files (HTML, CSS, JS, SVG)
            if mime_type and (mime_type.startswith("text") or mime_type.startswith("application/javascript")  or mime_type == "image/svg+xml" or mime_type == "application/json"):
                # Check minified size to decide gzip vs string
                with open(file_path, "r", encoding="utf-8") as f:
                    raw = f.read()
                minified = minimize_text(raw, file_path)

                if len(minified) > GZIP_THRESHOLD:
                    # Large file -> gzip only, no plain string (saves flash)
                    hex_array, gz_len = convert_to_gzip_array(file_path)
                    gzip_map[relative_path] = {
                        "data": f"const unsigned char {variable_name}_gz[] PROGMEM = {{ {hex_array} }};",
                        "len": f"const unsigned int {variable_name}_gz_len = {gz_len};"
                    }
                else:
                    content = convert_text_file(file_path)
                    text_map[relative_path] = f"const char {variable_name}[] PROGMEM = {content};"
            # Binary files (PNG, ICO)
            elif mime_type and mime_type.startswith("image"):
                content = convert_binary_file(file_path)
                binary_map[relative_path] = {
                    "data": f"const unsigned char {variable_name}[] PROGMEM = {{ {content} }};",
                    "len": f"const unsigned int {variable_name}_len = {len(content.split(','))};"
                }

    # Write the output C arrays and strings to a header file
    with open(output_file, "w", encoding="utf-8") as f_out:
        # Write text-based files
        f_out.write("#ifndef WEB_CONTENT_H\n")
        f_out.write("#define WEB_CONTENT_H\n\n")

        f_out.write("// Text-based files (HTML, CSS, JS)\n")
        for path, declaration in text_map.items():
            print(f"Conversion complete. Output written to {path}")
            f_out.write(f"// {path}\n{declaration}\n\n")

        # Write gzipped text files
        if gzip_map:
            f_out.write("// Gzipped text files (served with Content-Encoding: gzip)\n")
            for path, data in gzip_map.items():
                f_out.write(f"// {path} (gzipped)\n{data['data']}\n{data['len']}\n\n")

        # Write binary files
        f_out.write("// Binary files (PNG, ICO)\n")
        for path, data in binary_map.items():
            f_out.write(f"// {path}\n{data['data']}\n{data['len']}\n\n")

        f_out.write("#endif // WEB_CONTENT_H\n")

    # Write the map file
    with open(output_map_file, "w", encoding="utf-8") as f_map:

        f_map.write("#ifndef WEB_MAPPINGS_H\n")
        f_map.write("#define WEB_MAPPINGS_H\n\n")
        f_map.write("#include <map>\n")
        f_map.write("#include <Arduino.h>\n")
        f_map.write("#include \"web_content.h\"\n\n")
        # Write the stringMap for text files (only small files that are NOT gzipped)
        f_map.write("std::map<String, const char*> stringMap = {\n")
        for path, declaration in text_map.items():
            if path not in gzip_map:
                variable_name = os.path.splitext(path.replace("/", "_").replace(".", "_"))[0]
                f_map.write(f'    {{"{path}", {variable_name}}},\n')
        f_map.write("};\n\n")

        # Write the binaryMap for binary files
        f_map.write("struct BinaryFile {\n    const unsigned char* data;\n    unsigned int length;\n};\n\n")
        f_map.write("std::map<String, BinaryFile> binaryMap = {\n")
        for path, data in binary_map.items():
            variable_name = os.path.splitext(path.replace("/", "_").replace(".", "_"))[0]
            f_map.write(f'    {{"{path}", {{{variable_name}, {variable_name}_len}}}},\n')
        # Root-level favicon aliases: browsers may request `/favicon.ico` or
        # `/favicon.webp` directly (without the `images/` prefix). Only alias
        # the variants that actually exist — don't cross-serve, because the
        # runtime mime-type resolver picks Content-Type from the URL suffix.
        if "images/favicon.webp" in binary_map:
            f_map.write('    {"favicon.webp", {images_favicon_webp, images_favicon_webp_len}},\n')
        if "images/favicon.ico" in binary_map:
            f_map.write('    {"favicon.ico", {images_favicon_ico, images_favicon_ico_len}},\n')
        f_map.write("};\n\n")

        # Write the gzipMap for gzipped text files
        f_map.write("// Gzipped files - served with Content-Encoding: gzip\n")
        f_map.write("std::map<String, BinaryFile> gzipMap = {\n")
        for path in gzip_map:
            variable_name = os.path.splitext(path.replace("/", "_").replace(".", "_"))[0]
            f_map.write(f'    {{"{path}", {{{variable_name}_gz, {variable_name}_gz_len}}}},\n')
        f_map.write("};\n\n")

        f_map.write("#endif // WEB_MAPPINGS_H\n")
    print(f"Conversion complete. Output written to {output_file} and {output_map_file}")

def has_data_changed():
    """Check if any file in the data folder has been modified since the last build."""
    # Get the last build time (if the file exists)
    last_build_time = 0
    if os.path.exists(LAST_BUILD_FILE):
        with open(LAST_BUILD_FILE, "r", encoding="utf-8") as f:
            try:
                last_build_time = float(f.read().strip())
            except ValueError:
                last_build_time = 0

    # Get the latest modification time of files in the data folder
    data_files = glob.glob(os.path.join(web_folder, "**", "*"), recursive=True)
    if not data_files:
        return False  # No files to check, no need to run

    latest_mod_time = max(
        [os.path.getmtime(f) for f in data_files], default=0
    )
    print("latest_mod_time")
    print(latest_mod_time)
    print("last_build_time")
    print(last_build_time)

    # Return True if any file was modified after the last build
    return latest_mod_time > last_build_time

def update_last_build_time():
    """Update the last build timestamp after a successful build."""
    with open(LAST_BUILD_FILE, "w", encoding="utf-8") as f:
         f.write(str(time.time()))

web_folder = "data"  # Path to the folder containing HTML, CSS, JS, Images
output_header_file = "src/web_content.h"    # The output C header file for arrays/strings
output_map_file = "src/web_mappings.h"          # The output file for the maps
LAST_BUILD_FILE = ".last_build.txt" 

if has_data_changed():
    print("Data files have changed. Calling process_folder ......")
    process_folder(web_folder, output_header_file, output_map_file)

# else:
#     print("No changes in data files. Skipping preprocess...")

# Hook to update the timestamp after a successful build
def after_build(source, target, env):
    update_last_build_time()
    build_number = get_build_number()
    print(f"Build Number: {build_number}")
    save_build_number(build_number)
    archive_elf(env)

def archive_elf(env):
    """Copy the built .elf to ELF_ARCHIVE_DIR so coredumps can be decoded later."""
    progname = env.subst("${PROGNAME}")
    elf_path = env.subst("$BUILD_DIR/${PROGNAME}.elf")
    if not os.path.isfile(elf_path):
        print(f"archive_elf: source not found: {elf_path}")
        return
    try:
        os.makedirs(ELF_ARCHIVE_DIR, exist_ok=True)
        dest = os.path.join(ELF_ARCHIVE_DIR, f"{progname}.elf")
        shutil.copy2(elf_path, dest)
        print(f"archive_elf: {dest}")
    except OSError as e:
        print(f"archive_elf failed: {e}")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", after_build)

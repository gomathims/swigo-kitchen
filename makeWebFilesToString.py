import os
import re
import base64
import htmlmin
from csscompressor import compress
from jsmin import jsmin

# env = DefaultEnvironment()
def read_file(file_path):
    with open(file_path, 'r') as file:
        return file.read()

def read_binary_file(file_path):
    with open(file_path, 'rb') as file:
        return file.read()

def minify_html(content):
    return htmlmin.minify(content, remove_empty_space=True)

def minify_css(content):
    return compress(content)

def minify_js(content):
    return jsmin(content)

def to_c_string(content):
    escaped = content.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n')
    return f'"{escaped}"'


def to_c_binary(content):
    return ', '.join(f"0x{byte:02X}" for byte in content)

def to_base64(content):
    return base64.b64encode(content).decode('utf-8')

def path_to_variable_name(path):
    return re.sub(r'[^a-zA-Z0-9_]', '_', path).upper()

def process_files(directory):
    content_vars = {}
    file_mappings = {}
    
    for root, dirs, files in os.walk(directory):
        for filename in files:
            file_path = os.path.join(root, filename)
            relative_path = os.path.relpath(file_path, directory)
            url_path = '/' + relative_path.replace('\\', '/')
            
            if filename.endswith(('.html', '.css', '.js')):
                content = read_file(file_path)
                
                if filename.endswith('.html'):
                    minified = minify_html(content)
                elif filename.endswith('.css'):
                    minified = minify_css(content)
                elif filename.endswith('.js'):
                    minified = minify_js(content)
                
                var_name = path_to_variable_name(relative_path)
                content_vars[var_name] = to_c_string(minified)
                file_mappings[url_path] = var_name
            
            elif filename.lower().endswith(('.png', '.jpg', '.jpeg', '.gif', '.bmp')):
                content = read_binary_file(file_path)
                
                var_name = path_to_variable_name(relative_path)
                content_vars[var_name] = to_c_binary(content)
                file_mappings[url_path] = var_name
                
                # content = read_binary_file(file_path)
                # base64_content = to_base64(content)
                
                # var_name = f"IMAGE_{path_to_variable_name(relative_path)}"
                # content_vars[var_name] = to_c_string(base64_content)
                # file_mappings[url_path] = var_name
    
    return content_vars, file_mappings

def write_content_file(content_vars, output_file):
    with open(output_file, 'w') as file:
        file.write("#ifndef WEB_CONTENT_H\n")
        file.write("#define WEB_CONTENT_H\n\n")
        
        for name, content in content_vars.items():
            file.write(f"const unsigned char {name}[] = {{\n{content}}};\n\n")

            # Write the length of the array
            file.write(f"int {name}_data_len = {len(content)};\n")

        
        file.write("#endif // WEB_CONTENT_H\n")

def write_mapping_file(file_mappings, output_file, content_vars):
    with open(output_file, 'w') as file:
        file.write("#ifndef WEB_MAPPINGS_H\n")
        file.write("#define WEB_MAPPINGS_H\n\n")
        file.write("#include <map>\n")
        file.write("#include <Arduino.h>\n")
        file.write("#include \"web_content.h\"\n\n")

        file.write("std::map<String, int,  const char*> pathToContent = {\n")
        
        for path, var_name in file_mappings.items():
            file.write(f'    {{ String({to_c_string(path)}), {var_name}}},\n')
        
        file.write("};\n\n")
        file.write("#endif // WEB_MAPPINGS_H\n")

def before_build():
    # if __name__ == "__main__":
    input_directory = "data"  # Directory containing your web files
    content_output_file = "src/web_content.h"
    mapping_output_file = "src/web_mappings.h"
    
    content_vars, file_mappings = process_files(input_directory)
    write_content_file(content_vars, content_output_file)
    write_mapping_file(file_mappings, mapping_output_file, content_vars)
    print(f"Web content has been written to {content_output_file}")
    print(f"File mappings have been written to {mapping_output_file}")

# if env:
    # env.AddPreAction("$BUILD_DIR/${PROGNAME}.bin", before_build)

if __name__ == "__main__":
    before_build()
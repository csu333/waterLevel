import os

# Path to the file storing the version number
VERSION_FILE = 'version_number'
# Path to the header file that will be generated
VERSION_HEADER = 'include/version.h'

# 1. Read the current version
if not os.path.exists(VERSION_FILE):
    with open(VERSION_FILE, 'w') as f:
        f.write('0')
    current_version = 0
else:
    with open(VERSION_FILE, 'r') as f:
        current_version = int(f.read().strip())

# 2. Increment the version
new_version = current_version + 1

# 3. Save the new version back to the file
with open(VERSION_FILE, 'w') as f:
    f.write(str(new_version))

# 4. Generate the C++ header file
with open(VERSION_HEADER, 'w') as f:
    f.write(f'// AUTO-GENERATED - DO NOT EDIT\n')
    f.write(f'#define BUILD_VERSION {new_version}\n')

print(f'--- Incremented Build Version to: {new_version} ---')
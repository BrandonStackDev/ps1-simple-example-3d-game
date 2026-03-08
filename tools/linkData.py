import argparse;

#crete the parser and required args
parser = argparse.ArgumentParser(description="Create .s file linking .dat file to build")
parser.add_argument("name", help="variable name")
parser.add_argument("full_path", help="full data file path")

args = parser.parse_args()
name = args.name
fullPath = args.full_path.replace("\\", "/")

template = f'''
.section .rodata.{name}, "a"
.balign 8

.global {name}
.type {name}, @object
.size {name}, ({name}_end - {name})

{name}:
	.incbin "{fullPath}"
{name}_end:
'''

a = open(f'assets/inc/{name}.s','w')
a.write(template)
a.close()
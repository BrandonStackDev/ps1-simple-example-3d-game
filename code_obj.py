import random;
import argparse;

#crete the parser
parser = argparse.ArgumentParser(description="Parse Obj Files and spit out new file for PS1 draw objects.")

# Positional argument (required)
parser.add_argument("input", help="Input file path")
parser.add_argument("scale", help="scale factor")

# Optional flags
parser.add_argument("-o", "--output", default="code_out.txt", help="Output file path")

args = parser.parse_args()
in_path = args.input
out_path = args.output
scale = float(args.scale)
# this version is for tris
a = open(in_path,'r')
b = a.read()
a.close()

c = b.split('\n')

max_x=0
max_y=0
max_z=0
min_x=0
min_y=0
min_z=0

v = []
f = []

for x in c:
    if(len(x) > 0 and x[0]=='v' and x[1]==' '):
        #v -0.246787 18.897308 0.246787
        v.append(x.split(' ')[1:])
    if(len(x) > 0 and x[0]=='f'):
        #f 7/10/14 12/3/14 11/8/14 5/11/14
        tf = x.split(' ')[1:]
        kp = []
        for y in tf:
            stuff = y.split('/') #12/3/14
            kp.append(stuff[0])
        f.append(kp)
        #if(len(kp)==3):f.append(kp)
        #elif(len(kp)==4):
        # - f.append(kp[1:]) #this wont work to preserve backface culling, but I should be using 3 verts per face, tris, anyway always...ALWAYS!!!
        # - f.append(kp[:3])

# for x in v: print(x)
# for x in f: print(x)

s = ''
s+= 'static const GTEVector16 cubeVertices[NUM_CUBE_VERTICES] = {' + '\n'

for x in v:
    s+='    { .x = ' + str(int(float(x[0])*-scale)) + ', .y =  ' + str(int(float(x[1])*-scale)) + ', .z = ' + str(int(float(x[2])*-scale)) + ' }, \n'
s+= '};\n\n\n\n'

s += ''
s += ''
s += 'static const Face cubeFaces[NUM_CUBE_FACES] = { \n'
cl_store = 0x446600
for x in f:
    #--//random.randrange(0x1000000):06x
    # cl_store += random.randrange(2)
    # if(cl_store > 0x79eeee):cl_store = 0x66aa11
    # col=f"0x{cl_store:06x}"
    col=f"0x{random.randrange(0x600000):06x}"
    s+= '    { .vertices = { ' + str(int(x[0])-1) + ', ' + str(int(x[1])-1) + ', ' + str(int(x[2])-1) + ' }, .color = '+col+' }, \n' #
s+= '};\n'


print(len(v))
print(len(f))

a = open(out_path,'w')
a.write(s)
a.close()


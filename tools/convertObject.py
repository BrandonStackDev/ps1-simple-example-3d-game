import random
import argparse
import struct #H=16bit,h=8bit,I=32bit

#create the parser
parser = argparse.ArgumentParser(description="Parse Obj Files and spit out new file for PS1 draw objects.")

# Positional argument (required)
parser.add_argument("input", help="Input file path")
parser.add_argument("scale", help="scale factor")
parser.add_argument("output", help="Output file name (without ext, will be like out_vert.dat and out_face.dat)")
parser.add_argument("textureWidth", nargs='?', default=0, help="Width of texture")
parser.add_argument("textureHeight", nargs='?', default=0, help="Height of texture")

args = parser.parse_args()
in_path = args.input
out_path = args.output
scale = float(args.scale)
t_w = int(args.textureWidth)
t_h = int(args.textureHeight)
# this version is for tris
a = open(in_path,'r')
b = a.read()
a.close()
c = b.split('\n')

v = []
vt = []
f = []

for x in c:
    if(len(x) > 0 and x[0]=='v' and x[1]==' '):
        #v -0.246787 18.897308 0.246787
        v.append(x.split(' ')[1:])
    if(len(x) > 0 and x[0]=='v' and x[1]=='t'):
        vt.append(x.split(' ')[1:])
    if(len(x) > 0 and x[0]=='f'):
        #f 7/10/14 12/3/14 11/8/14
        #v_index / vt_index / vn_index
        tf = x.split(' ')[1:]
        kp = []
        for y in tf:
            stuff = y.split('/') #12/3/14
            kp.append(stuff[0]+stuff[1]) #[v1,v2,v3,t1,t2,t3]
        f.append(kp)


print(len(v))
a = open(f'assets/dat/{out_path}_verts.dat','wb')
for x in v:
    # GTEVector16:
    # int16_t x, y, z, _padding;
    a.write(struct.pack("<hhhh", int(float(x[0])*-scale), int(float(x[1])*-scale), int(float(x[2])*-scale), 0))
a.close()

if(t_w != 0 and t_h != 0):
    print(len(vt))
    a = open(f'assets/dat/{out_path}_vert_text.dat','wb')
    for x in vt:
        a.write(struct.pack("<HH", 
            int(float(x[0])*t_w), #u, width
            int((1-float(x[1]))*t_h))) #v, height, flipped for PS1
    a.close()

print(len(f))
a = open(f'assets/dat/{out_path}_faces.dat','wb')
for x in f:
    # Face:
    # uint16_t vertices[3];
    # uint16_t textCoords[3];
    # uint32_t color;
    #a.write(struct.pack("<HHHHI", int(x[0])-1, int(x[1])-1, int(x[2])-1, 0, random.randrange(0x600000))) #orig
    tc1 = 0
    tc2 = 0
    tc3 = 0
    if(len(x)>3):
        tc1 = int(x[3])-1
        tc1 = int(x[4])-1
        tc1 = int(x[5])-1
    a.write(struct.pack("<HHHHHHI", 
        int(x[0])-1, int(x[1])-1, int(x[2])-1, #verts indices
        tc1,tc2,tc3, #text coords indices
        random.randrange(0x600000))) #random color
a.close()

print(':)')



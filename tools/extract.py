import numpy as np
from PIL import Image
from scipy import ndimage
U='/mnt/user-data/uploads/'
sheet=np.array(Image.open(U+'IMG_6467.png').convert('RGB')).astype(int)
def cut(x0,y0,x1,y1):
    c=sheet[y0:y1,x0:x1]
    bg=np.array([219,248,220])
    # background = light, low-saturation pale green; flood from border
    d=np.sqrt(((c-bg)**2).sum(-1))
    cand=d<38
    lab,n=ndimage.label(cand)
    border=set(np.unique(np.concatenate([lab[0],lab[-1],lab[:,0],lab[:,-1]])))-{0}
    bgm=np.isin(lab,list(border))
    fg=~bgm
    lab2,n2=ndimage.label(fg)
    sizes=ndimage.sum(fg,lab2,range(1,n2+1))
    keep=lab2==(np.argmax(sizes)+1)
    keep=ndimage.binary_fill_holes(keep)
    ys,xs=np.where(keep)
    y0b,y1b,x0b,x1b=ys.min(),ys.max()+1,xs.min(),xs.max()+1
    rgba=np.zeros((y1b-y0b,x1b-x0b,4),np.uint8)
    rgba[...,:3]=c[y0b:y1b,x0b:x1b]
    rgba[...,3]=keep[y0b:y1b,x0b:x1b]*255
    return Image.fromarray(rgba)
parts={
 'arm_upper_L':cut(60,465,235,640),
 'arm_upper_R':cut(355,465,530,640),
 'thigh0':cut(640,470,785,610),'thigh1':cut(800,470,915,610),
 'thigh2':cut(960,470,1075,610),'thigh3':cut(1085,470,1225,610),
 'shin0':cut(645,615,745,820),'shin1':cut(820,615,905,820),
 'shin2':cut(955,615,1050,820),'shin3':cut(1100,615,1205,820),
}
for k,v in parts.items(): v.save(k+'.png'); print(k,v.size)
m={'torso':'1790486048079','head':'1790486066341','abdomen':'1790486079259','scythe_A':'1790486113416','scythe_B':'1790486125055'}
for k,f in m.items():
    im=Image.open(U+f+'_image.png').convert('RGBA'); a=np.array(im)
    a[...,3]=np.where(a[...,3]>=128,255,0)
    im=Image.fromarray(a); im=im.crop(im.getbbox()); im.save(k+'.png'); print(k,im.size)

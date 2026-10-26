import os
import random
import math
from PIL import Image, ImageDraw, ImageFilter

def draw_star(draw, cx, cy, size, rotation, color):
    """Draw a 5-pointed star"""
    points = []
    for i in range(5):
        angle = math.radians(rotation + i * 72 - 90)
        points.append((cx + size * math.cos(angle), cy + size * math.sin(angle)))
        angle = math.radians(rotation + i * 72 - 90 + 36)
        points.append((cx + size * 0.4 * math.cos(angle), cy + size * 0.4 * math.sin(angle)))
    draw.polygon(points, fill=color)

def draw_triangle(draw, cx, cy, size, rotation, color):
    """Draw an equilateral triangle"""
    points = []
    for i in range(3):
        angle = math.radians(rotation + i * 120 - 90)
        points.append((cx + size * math.cos(angle), cy + size * math.sin(angle)))
    draw.polygon(points, fill=color)

def draw_square(draw, cx, cy, size, rotation, color):
    """Draw a rotated square"""
    points = []
    for i in range(4):
        angle = math.radians(rotation + i * 90 - 45)
        points.append((cx + size * 0.7 * math.cos(angle), cy + size * 0.7 * math.sin(angle)))
    draw.polygon(points, fill=color)

def draw_ring_proper(draw, cx, cy, size, rotation, color):
    """Draw a ring by drawing many small quads between inner and outer radius"""
    outer = size
    inner = size * 0.5
    steps = 48
    rot_rad = math.radians(rotation)
    for i in range(steps):
        a1 = 2 * math.pi * i / steps + rot_rad
        a2 = 2 * math.pi * (i + 1) / steps + rot_rad
        quad = [
            (cx + inner * math.cos(a1), cy + inner * math.sin(a1)),
            (cx + outer * math.cos(a1), cy + outer * math.sin(a1)),
            (cx + outer * math.cos(a2), cy + outer * math.sin(a2)),
            (cx + inner * math.cos(a2), cy + inner * math.sin(a2)),
        ]
        draw.polygon(quad, fill=color)

def draw_shape(draw, shape_type, cx, cy, size, rotation, color):
    if shape_type == 'star':
        draw_star(draw, cx, cy, size, rotation, color)
    elif shape_type == 'triangle':
        draw_triangle(draw, cx, cy, size, rotation, color)
    elif shape_type == 'square':
        draw_square(draw, cx, cy, size, rotation, color)
    elif shape_type == 'ring':
        draw_ring_proper(draw, cx, cy, size, rotation, color)

def generate_image(seed):
    """Generate one 32x32 image"""
    random.seed(seed)
    
    W = H = 32
    SUPERSAMPLE = 4
    SW, SH = W * SUPERSAMPLE, H * SUPERSAMPLE
    
    shapes_layer = Image.new('RGBA', (SW, SH), (0, 0, 0, 0))
    shapes_draw = ImageDraw.Draw(shapes_layer)
    
    shape_types = ['star', 'triangle', 'square', 'ring']
    colors = [
        (255, 80, 80, 255),
        (80, 160, 255, 255),
        (255, 220, 60, 255),
        (100, 220, 120, 255),
        (200, 100, 255, 255),
        (255, 160, 60, 255),
        (60, 220, 220, 255),
        (255, 100, 180, 255),
    ]
    
    chosen = [random.choice(shape_types) for _ in range(3)]
    
    for stype in chosen:
        margin = int(SW * 0.2)
        cx = random.randint(margin, SW - margin)
        cy = random.randint(margin, SH - margin)
        size = random.randint(int(SW * 0.15), int(SW * 0.32))
        rotation = random.uniform(0, 360)
        color = random.choice(colors)
        draw_shape(shapes_draw, stype, cx, cy, size, rotation, color)
    
    alpha = shapes_layer.split()[3]
    outline_width = 2 * SUPERSAMPLE
    dilated = alpha.filter(ImageFilter.MaxFilter(outline_width * 2 + 1))
    
    final = Image.new('RGBA', (SW, SH), (0, 0, 0, 0))
    white_layer = Image.new('RGBA', (SW, SH), (255, 255, 255, 255))
    final.paste(white_layer, (0, 0), dilated)
    final.paste(shapes_layer, (0, 0), shapes_layer)
    
    final = final.resize((W, H), Image.LANCZOS)
    
    return final

def main():
    out_dir = "output"
    os.makedirs(out_dir, exist_ok=True)
    
    for i in range(1, 100):
        img = generate_image(seed=i * 1337 + 42)
        filename = f"icon_{i:02d}.png"
        img.save(os.path.join(out_dir, filename), "PNG")
        print(f"Generated {filename}")
    
    print("Done!")

if __name__ == "__main__":
    main()
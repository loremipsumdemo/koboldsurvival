import colorsys
import math
import random
from pathlib import Path

from PIL import Image, ImageDraw

SIZE = 256
IMAGE_COUNT = 100
SHAPES_PER_IMAGE = 4
OUTPUT_DIR = Path("img")
OUTLINE_WIDTH = 12
MARGIN = 36


def vivid_color(hue):
    """Return a fully saturated RGB color for a hue from 0 to 1."""
    return tuple(round(channel * 255) for channel in colorsys.hsv_to_rgb(hue % 1.0, 1.0, 1.0))


def star_points(cx, cy, outer_radius, inner_radius, points=5, rotation=-math.pi / 2):
    result = []
    for i in range(points * 2):
        radius = outer_radius if i % 2 == 0 else inner_radius
        angle = rotation + i * math.pi / points
        result.append((cx + math.cos(angle) * radius, cy + math.sin(angle) * radius))
    return result


def draw_random_shape(draw, rng, hue):
    shape_type = rng.choice(("circle", "rectangle", "triangle", "star", "diamond", "hexagon"))
    x1 = rng.randint(MARGIN, SIZE - MARGIN - 120)
    y1 = rng.randint(MARGIN, SIZE - MARGIN - 120)
    width = rng.randint(90, min(220, SIZE - MARGIN - x1))
    height = rng.randint(90, min(220, SIZE - MARGIN - y1))
    x2, y2 = x1 + width, y1 + height
    color = vivid_color(hue)
    outline = (255, 255, 255, 255)
    fill = (*color, 255)

    if shape_type == "circle":
        draw.ellipse((x1, y1, x2, y2), fill=fill, outline=outline, width=OUTLINE_WIDTH)
    elif shape_type == "rectangle":
        draw.rounded_rectangle(
            (x1, y1, x2, y2), radius=rng.randint(0, 28),
            fill=fill, outline=outline, width=OUTLINE_WIDTH
        )
    elif shape_type == "triangle":
        points = ((x1 + width / 2, y1), (x2, y2), (x1, y2))
        draw.polygon(points, fill=fill)
        draw.line((*points, points[0]), fill=outline, width=OUTLINE_WIDTH, joint="curve")
    elif shape_type == "star":
        cx, cy = (x1 + x2) / 2, (y1 + y2) / 2
        radius = min(width, height) / 2
        points = star_points(cx, cy, radius, radius * 0.45, rng.randint(5, 8))
        draw.polygon(points, fill=fill)
        draw.line((*points, points[0]), fill=outline, width=OUTLINE_WIDTH, joint="curve")
    elif shape_type == "diamond":
        points = ((x1 + width / 2, y1), (x2, y1 + height / 2),
                  (x1 + width / 2, y2), (x1, y1 + height / 2))
        draw.polygon(points, fill=fill)
        draw.line((*points, points[0]), fill=outline, width=OUTLINE_WIDTH, joint="curve")
    else:
        points = []
        for i in range(6):
            angle = -math.pi / 2 + i * math.pi / 3
            points.append(((x1 + x2) / 2 + math.cos(angle) * width / 2,
                           (y1 + y2) / 2 + math.sin(angle) * height / 2))
        draw.polygon(points, fill=fill)
        draw.line((*points, points[0]), fill=outline, width=OUTLINE_WIDTH, joint="curve")


def generate_images():
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    rng = random.Random()

    for index in range(1, IMAGE_COUNT + 1):
        image = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
        draw = ImageDraw.Draw(image)

        # Start at a random hue, then shift hue for each shape.
        start_hue = rng.random()
        hue_step = rng.uniform(0.12, 0.32)
        hues = [(start_hue + i * hue_step + rng.uniform(-0.04, 0.04)) % 1.0
                for i in range(SHAPES_PER_IMAGE)]
        rng.shuffle(hues)

        for hue in hues:
            draw_random_shape(draw, rng, hue)

        output_path = OUTPUT_DIR / f"item_{index}.png"
        image.save(output_path, format="PNG", optimize=True)

    print(f"Generated {IMAGE_COUNT} transparent {SIZE}x{SIZE} PNGs in: {OUTPUT_DIR.resolve()}")


if __name__ == "__main__":
    try:
        generate_images()
    except ImportError as exc:
        raise SystemExit("Missing dependency. Install it with: python -m pip install Pillow") from exc

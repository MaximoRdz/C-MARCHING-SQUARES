#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <math.h>


#include "../include/raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "../include/raygui.h"


/*
 *
 * bit 3 = top-left     (8)
 * bit 2 = top-right    (4)
 * bit 1 = bottom-right (2)
 * bit 0 = bottom-left  (1)
 *
 * */
typedef enum {
    EDGE_NONE = -1,
    EDGE_TOP,
    EDGE_RIGHT,
    EDGE_BOTTOM,
    EDGE_LEFT,
} Edge;


typedef struct {
    Edge a, b;
} Segment;


typedef struct {
    size_t count;
    Segment segs[2];
} CellContours;


CellContours LUT[16] = {
    [0]  = {0, {{EDGE_NONE, EDGE_NONE}}},                                                  // 0000
    [1]  = {1, {{EDGE_LEFT, EDGE_BOTTOM}}},                                                // 0001
    [2]  = {1, {{EDGE_BOTTOM, EDGE_RIGHT}}},                                               // 0010
    [3]  = {1, {{EDGE_LEFT, EDGE_RIGHT}}},                                                 // 0011
    [4]  = {1, {{EDGE_TOP, EDGE_RIGHT}}},                                                  // 0100
    [5]  = {2, {{EDGE_TOP, EDGE_LEFT}, {EDGE_BOTTOM, EDGE_RIGHT}}},                        // 0101 (ambiguous)
    [6]  = {1, {{EDGE_TOP, EDGE_BOTTOM}}},                                                 // 0110
    [7]  = {1, {{EDGE_TOP, EDGE_LEFT}}},                                                   // 0111
    [8]  = {1, {{EDGE_LEFT, EDGE_TOP}}},                                                   // 1000
    [9]  = {1, {{EDGE_TOP, EDGE_BOTTOM}}},                                                 // 1001
    [10] = {2, {{EDGE_TOP, EDGE_RIGHT}, {EDGE_BOTTOM, EDGE_LEFT}}},                        // 1010 (ambiguous)
    [11] = {1, {{EDGE_TOP, EDGE_RIGHT}}},                                                  // 1011
    [12] = {1, {{EDGE_LEFT, EDGE_RIGHT}}},                                                 // 1100
    [13] = {1, {{EDGE_BOTTOM, EDGE_RIGHT}}},                                               // 1101
    [14] = {1, {{EDGE_LEFT, EDGE_BOTTOM}}},                                                // 1110
    [15] = {0, {{EDGE_NONE, EDGE_NONE}}}                                                   // 1111
};


typedef struct {
    uint8_t isovalue;
    Vector2 a, b;
} LineSegment;


typedef struct {
    size_t capacity;
    size_t initial_capacity;
    size_t count;
    LineSegment *arr;
} DynamicArray;


bool darray_init(DynamicArray* darray, size_t initial_capacity)
{
    LineSegment *segments = malloc(initial_capacity * sizeof(LineSegment));
    if (!segments) {
        fprintf(stderr, "Couldn't allocate LineSegment array of lenght %zu\n", initial_capacity);
        return false;
    }


    darray->arr = segments;
    darray->count = 0;
    darray->capacity = initial_capacity;
    darray->initial_capacity = initial_capacity;

    return true;
}


void darray_free(DynamicArray* darray)
{
    if (!darray) return;

    free(darray->arr);
    darray->arr = NULL;
    darray->count = 0;
    darray->initial_capacity = 0;
    darray->capacity = 0;
}


bool darray_resize(DynamicArray* darray)
{
    size_t new_capacity = darray->capacity * 2;

    LineSegment *reallocation =
        realloc(darray->arr, sizeof(LineSegment) * new_capacity);

    if (!reallocation) {
        fprintf(stderr, "Reallocation failed (previous capacity: %zu)\n", darray->capacity);
        return false;
    }

    darray->arr = reallocation;
    darray->capacity = new_capacity;

    return true;
}


bool darray_reset(DynamicArray* darray)
{
    // size_t initial_capacity = darray->initial_capacity;
    // darray_free(darray);
    // return darray_init(darray, initial_capacity);
    darray->count = 0;  // faster...
    return true;
}


void darray_push(DynamicArray* darray, LineSegment lseg)
{
    if (darray->capacity <= darray->count) {
        if (!darray_resize(darray)) {
            fprintf(stderr, "Fatal: dynamic array resize failed\n");
            exit(1);
        }
    }

    darray->arr[darray->count++] = lseg;
}


/*
    Linear Interpolation: Assume isovalue varies linearly and interpolate
    the location along the edge where the contour intersects, instead of 
    just using the naive "middle" point of the edge.

    coord_iso = coord_1 + mu * (coord_2 - coord_1)
    where mu = (ISO - value_1) / (value_2 - value_1)
*/
Vector2 interpolate(
        uint8_t isovalue,
        Vector2 p1,
        Vector2 p2,
        uint8_t v1,
        uint8_t v2
    )
{
    if (isovalue == v1) return p1;
    if (isovalue == v2) return p2;
    if (v1 == v2) return p1;

    float mu = (isovalue - v1) / (float)(v2 - v1);

    return (Vector2){
        p1.x + mu * (p2.x - p1.x),
        p1.y + mu * (p2.y - p1.y)
    };
}


// no interpolation approach.
Vector2 edge_to_point(
        float y,
        float x,
        Edge edge
    )
{
    switch (edge) {
        case EDGE_TOP:    return (Vector2){ x, y - 0.5f };
        case EDGE_RIGHT:  return (Vector2){ x + 0.5f, y };
        case EDGE_BOTTOM: return (Vector2){ x, y + 0.5f };
        case EDGE_LEFT:   return (Vector2){ x - 0.5f, y };
        default: return (Vector2){ 0, 0 };
    }
}

Vector2 edge_interp(
    Edge edge,
    float x, float y,
    uint8_t v_tl, uint8_t v_tr,
    uint8_t v_bl, uint8_t v_br,
    uint8_t isovalue
)
{
    Vector2 p1, p2;
    uint8_t val1, val2;

    switch (edge) {
        case EDGE_TOP:
            p1 = (Vector2){ x - 0.5f, y - 0.5f }; // top-left
            p2 = (Vector2){ x + 0.5f, y - 0.5f }; // top-right
            val1 = v_tl;
            val2 = v_tr;
            break;

        case EDGE_RIGHT:
            p1 = (Vector2){ x + 0.5f, y - 0.5f }; // top-right
            p2 = (Vector2){ x + 0.5f, y + 0.5f }; // bottom-right
            val1 = v_tr;
            val2 = v_br;
            break;

        case EDGE_BOTTOM:
            p1 = (Vector2){ x - 0.5f, y + 0.5f }; // bottom-left
            p2 = (Vector2){ x + 0.5f, y + 0.5f }; // bottom-right
            val1 = v_bl;
            val2 = v_br;
            break;

        case EDGE_LEFT:
            p1 = (Vector2){ x - 0.5f, y - 0.5f }; // top-left
            p2 = (Vector2){ x - 0.5f, y + 0.5f }; // bottom-left
            val1 = v_tl;
            val2 = v_bl;
            break;

        default:
            return (Vector2){0,0};
    }

    return interpolate(isovalue, p1, p2, val1, val2);
}

void marching_squares(Image *image, uint8_t isovalue, DynamicArray *output_contours)
{
    // contouring grid size: (W-1, H-1) 
    size_t i, j;
    size_t top_left, top_right, bottom_left, bottom_right;
    size_t cell_index;
    CellContours cell_type;
    Vector2 p1, p2;

    size_t width = image->width;
    size_t height = image->height;
    uint8_t *image_data = (uint8_t *)image->data;
    LineSegment segment;

    float x, y;
    uint8_t v_tl, v_tr, v_bl, v_br;

    for (size_t c=0; c<((width - 1) * (height - 1)); c++) {
        // for each cell visit its 4 corner pixels clockwise
        i = c / (width - 1);
        j = c % (width - 1);

        //  image grid indices
        top_left = i * width + j;
        top_right = i * width + (j + 1);
        bottom_left = (i + 1) * width + j;
        bottom_right = (i + 1) * width + (j + 1);

        cell_index = 0;
        cell_index |= (image_data[top_left]     > isovalue)  << 3;
        cell_index |= (image_data[top_right]    > isovalue)  << 2;
        cell_index |= (image_data[bottom_right] > isovalue)  << 1;
        cell_index |= (image_data[bottom_left]  > isovalue)  << 0;

        cell_type = LUT[cell_index];

        // cell indices (contouring grid) are offset wrt image grid.
        y = (float)i + 0.5f;
        x = (float)j + 0.5f;

        for (size_t s=0; s<cell_type.count; s++) {
            // p1 = edge_to_point(y, x, cell_type.segs[s].a);
            // p2 = edge_to_point(y, x, cell_type.segs[s].b);

            v_tl = image_data[top_left];
            v_tr = image_data[top_right];
            v_bl = image_data[bottom_left];
            v_br = image_data[bottom_right];

            p1 = edge_interp(cell_type.segs[s].a, x, y,
                             v_tl, v_tr, v_bl, v_br, isovalue);

            p2 = edge_interp(cell_type.segs[s].b, x, y,
                             v_tl, v_tr, v_bl, v_br, isovalue);

            segment.a = p1;
            segment.b = p2;
            segment.isovalue = isovalue;

            darray_push(output_contours, segment);
        }
    }
}


void render_contours(DynamicArray *contours, Color color)
{
    LineSegment segment;
    for (size_t i=0; i<contours->count; i++)
    {
        segment = contours->arr[i];
        DrawLine(segment.a.x, segment.a.y, segment.b.x, segment.b.y, color); // TBD: map isovalue to color
    }
}


int main(void)
{
    srand(time(NULL));
    // SetConfigFlags(FLAG_WINDOW_RESIZABLE);

    // char *image_path = "./assets/shape.png";
    char *image_path = "./assets/cangas-de-onis.jpg";

    Image image = LoadImage(image_path);
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);
    ImageResize(&image, image.width/1.5, image.height/1.5);

    printf("loaded image dims after resizing: %d x %d\n", image.width, image.height);
    printf("bytes size of float: %zu\n", sizeof(LineSegment));
    size_t length = image.width * image.height;

    InitWindow(image.width, image.height, "Interactive Marching Squares");
    SetTargetFPS(60);

    uint8_t isovalue = 200;
    uint8_t *bool_field = malloc(length * sizeof(uint8_t));
    uint8_t *debug_field = malloc(length * sizeof(uint8_t));

    if (!bool_field) {
        fprintf(stderr, "ERROR: could not allocate bool_field\n");
        return 1;
    }
    if (!debug_field) {
        fprintf(stderr, "ERROR: could not allocate debug_field\n");
        return 1;
    }

    DynamicArray *output_contours  = malloc(sizeof(DynamicArray));
    
    if (!darray_init(output_contours, (size_t)(image.height * image.width / 2))) {
        fprintf(stderr, "ERROR: could not initialize output_contours\n");
        return 1;
    }

    DynamicArray *output_contours_top  = malloc(sizeof(DynamicArray));
    
    if (!darray_init(output_contours_top, (size_t)(image.height * image.width / 2))) {
        fprintf(stderr, "ERROR: could not initialize output_contours\n");
        return 1;
    }


    Texture2D texture = LoadTextureFromImage(image);
    
    bool show_threshold = false; 

    float value = (float)isovalue;
    float minValue = 0.f;
    float maxValue = 255.0f;
    size_t slider_text_update_size;
    char slider_text[20];

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_SPACE)) {
            // change base image: gray image <-> thresholded image
            show_threshold = !show_threshold;

            uint8_t *img_data = (uint8_t *)image.data;

            for (size_t i=0; i<length; i++) {
                bool_field[i] = (uint8_t) (img_data[i] > isovalue);
                debug_field[i] = bool_field[i] ? 255 : 0;
            }
            UpdateTexture(
                    texture,
                    show_threshold ? debug_field : img_data
                );
        }

        if (IsKeyDown(KEY_UP) && isovalue < 255) {
            isovalue++;
            value = (float)isovalue;
        }
        if (IsKeyDown(KEY_DOWN) && isovalue > 0) {
            isovalue--;
            value = (float)isovalue;
        }

        BeginDrawing();
            ClearBackground(RAYWHITE);

            DrawTexture(texture, 0, 0, WHITE);

            if (!darray_reset(output_contours)) {
                fprintf(stderr, "Dynamic array reset failed...\n");
                return 1;
            }

            if (!darray_reset(output_contours_top)) {
                fprintf(stderr, "Dynamic array reset failed...\n");
                return 1;
            }

            marching_squares(&image, isovalue, output_contours);
            marching_squares(&image, isovalue+10, output_contours_top);
            render_contours(output_contours, BLUE);
            render_contours(output_contours_top, RED);

            GuiSlider(
                (Rectangle){10, 10, image.width - 20, 25},  // x y witdth height
                NULL,                           
                NULL,                          
                &value,                       
                minValue,                    
                maxValue                    
            );

            isovalue = (uint8_t)value;
            slider_text_update_size = snprintf(slider_text, sizeof(slider_text), "Isovalue: %u", isovalue);
            if (slider_text_update_size >= sizeof(slider_text)) {
                printf("WARNING: sliter text isovalue needed truncation (needed %zu chars)\n", slider_text_update_size);
            }
            DrawText(slider_text, 20, 12, 20, BLACK);
        EndDrawing();
    }
    UnloadTexture(texture);
    UnloadImage(image);
    CloseWindow();
    free(bool_field);
    free(debug_field);

    darray_free(output_contours);
    free(output_contours);

    darray_free(output_contours_top);
    free(output_contours_top);

    return 0;
}

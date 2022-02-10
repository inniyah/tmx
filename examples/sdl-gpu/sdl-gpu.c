#include <stdio.h>
#include <tmx.h>
#include <SDL.h>
#include <SDL_gpu.h>
#include <SDL_events.h>
#include <SDL_image.h>

#define DISPLAY_H 600
#define DISPLAY_W 800

/*
 * Here is a list of most of the comparable functions:
 *
 * SDL_CreateWindow() : Either use GPU_SetInitWindow() or replace with GPU_Init()
 * SDL_CreateRenderer() : GPU_Init()
 * SDL_LoadBMP() : GPU_LoadImage() or GPU_LoadSurface()
 * SDL_CreateTextureFromSurface() : GPU_CopyImageFromSurface()
 * SDL_SetRenderDrawColor() : Pass color into rendering function (e.g. GPU_ClearRGBA(), GPU_Line())
 * SDL_RenderClear() : GPU_Clear(), GPU_ClearRGBA()
 * SDL_QueryTexture() : image->w, image->h
 * SDL_RenderCopy() : GPU_Blit() or GPU_BlitRect()
 * SDL_RenderPresent() : GPU_Flip()
 * SDL_DestroyTexture() : GPU_FreeImage()
 * SDL_DestroyRenderer() : GPU_FreeTarget() (but don't free the screen target yourself)
 *
 * SDL_RenderDrawPoint() : GPU_Pixel()
 * SDL_RenderDrawLine() : GPU_Line()
 * SDL_RenderDrawRect() : GPU_Rectangle()
 * SDL_RenderFillRect() : GPU_RectangleFilled()
 * SDL_RenderCopyEx() : GPU_BlitRotate() or GPU_BlitScale() or GPU_BlitTransform()
 * SDL_SetRenderDrawBlendMode() : GPU_SetShapeBlendMode()
 * SDL_SetTextureBlendMode() : GPU_SetBlendMode()
 * SDL_SetTextureColorMod() : GPU_SetRGBA() or GPU_SetColor()
 * SDL_SetTextureAlphaMod() : GPU_SetRGBA() or GPU_SetColor()
 * SDL_UpdateTexture() : GPU_UpdateImage() or GPU_UpdateImageBytes()
 * SDL_RenderSetClipRect() : GPU_SetClip() or GPU_SetClipRect()
 * SDL_RenderReadPixels() : GPU_CopySurfaceFromTarget() or GPU_CopySurfaceFromImage()
 * SDL_RenderSetViewport() : GPU_SetViewport()
 * SDL_SetRenderTarget() : GPU_LoadTarget()
 */

void* SDL_tex_loader(const char *path) {
	GPU_Image *image = GPU_LoadImage(path);
	return image;
}

void draw_polyline(GPU_Target *screen, double **points, double x, double y, int pointsc, SDL_Color color) {
	int i;
	for (i=1; i<pointsc; i++) {
		GPU_Line(screen, x+points[i-1][0], y+points[i-1][1], x+points[i][0], y+points[i][1], color);
	}
}

void draw_polygon(GPU_Target *screen, double **points, double x, double y, int pointsc, SDL_Color color) {
	draw_polyline(screen, points, x, y, pointsc, color);
	if (pointsc > 2) {
		GPU_Line(screen, x+points[0][0], y+points[0][1], x+points[pointsc-1][0], y+points[pointsc-1][1], color);
	}
}

void draw_objects(GPU_Target *screen, tmx_object_group *objgr) {
	tmx_col_bytes col = tmx_col_to_bytes(objgr->color);
	SDL_Color color = {.r = col.r, .g = col.g, .b = col.b, .a = 255};

	tmx_object *head = objgr->head;
	while (head) {
		if (head->visible) {
			if (head->obj_type == OT_SQUARE) {
				GPU_Rectangle(screen, head->x, head->y, head->x + head->width, head->y + head->height, color);
			}
			else if (head->obj_type  == OT_POLYGON) {
				draw_polygon(screen, head->content.shape->points, head->x, head->y, head->content.shape->points_len, color);
			}
			else if (head->obj_type == OT_POLYLINE) {
				draw_polyline(screen, head->content.shape->points, head->x, head->y, head->content.shape->points_len, color);
			}
			else if (head->obj_type == OT_ELLIPSE) {
				GPU_Ellipse(screen, head->x + head->width/2.0, head->y + head->height/2.0, head->width/2.0, head->height/2.0, 360.0, color);
			}
		}
		head = head->next;
	}
}

void draw_tile(GPU_Target *screen, void *image, unsigned int sx, unsigned int sy, unsigned int sw, unsigned int sh,
               unsigned int dx, unsigned int dy, float opacity, unsigned int flags) {
	GPU_Rect src_rect, dest_rect;
	src_rect.x = sx;
	src_rect.y = sy;
	src_rect.w = dest_rect.w = sw;
	src_rect.h = dest_rect.h = sh;
	dest_rect.x = dx;
	dest_rect.y = dy;
	GPU_BlitRect((GPU_Image*)image, &src_rect, screen, &dest_rect);
}

void draw_layer(GPU_Target *screen, tmx_map *map, tmx_layer *layer) {
	unsigned long i, j;
	unsigned int gid, x, y, w, h, flags;
	float op;
	tmx_tileset *ts;
	tmx_image *im;
	void* image;
	op = layer->opacity;
	for (i=0; i<map->height; i++) {
		for (j=0; j<map->width; j++) {
			gid = (layer->content.gids[(i*map->width)+j]) & TMX_FLIP_BITS_REMOVAL;
			if (map->tiles[gid] != NULL) {
				ts = map->tiles[gid]->tileset;
				im = map->tiles[gid]->image;
				x  = map->tiles[gid]->ul_x;
				y  = map->tiles[gid]->ul_y;
				w  = ts->tile_width;
				h  = ts->tile_height;
				if (im) {
					image = im->resource_image;
				}
				else {
					image = ts->image->resource_image;
				}
				flags = (layer->content.gids[(i*map->width)+j]) & ~TMX_FLIP_BITS_REMOVAL;
				draw_tile(screen, image, x, y, w, h, j*ts->tile_width, i*ts->tile_height, op, flags);
			}
		}
	}
}

void draw_image_layer(GPU_Target *screen, tmx_image *image) {
	GPU_Image *texture = (GPU_Image*)image->resource_image; // Texture loaded by libTMX
	GPU_Rect dim;
	dim.x = dim.y = 0;
	dim.w = texture->w;
	dim.h = texture->h;
	GPU_Blit(texture, &dim, screen, dim.x, dim.y);
}

void draw_all_layers(GPU_Target *screen, tmx_map *map, tmx_layer *layers) {
	while (layers) {
		if (layers->visible) {
			if (layers->type == L_GROUP) {
				draw_all_layers(screen, map, layers->content.group_head);
			}
			else if (layers->type == L_OBJGR) {
				draw_objects(screen, layers->content.objgr);
			}
			else if (layers->type == L_IMAGE) {
				draw_image_layer(screen, layers->content.image);
			}
			else if (layers->type == L_LAYER) {
				draw_layer(screen, map, layers);
			}
		}
		layers = layers->next;
	}
}

void render_map(GPU_Target *screen, tmx_map *map) {
	tmx_col_bytes col = tmx_col_to_bytes(map->backgroundcolor);
	GPU_ClearRGBA(screen, col.r, col.g, col.b, col.a);
	draw_all_layers(screen, map, map->ly_head);
}

Uint32 timer_func(Uint32 interval, void *param) {
	SDL_Event event;
	SDL_UserEvent userevent;

	userevent.type = SDL_USEREVENT;
	userevent.code = 0;
	userevent.data1 = NULL;
	userevent.data2 = NULL;

	event.type = SDL_USEREVENT;
	event.user = userevent;

	SDL_PushEvent(&event);
	return(interval);
}

static void main_loop(GPU_Target* screen, tmx_map *map) {
	Uint8 done;
	SDL_Event event;
    
	done = 0;
	while (!done) {
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				done = 1;
			} else if (event.type == SDL_KEYDOWN) {
				if (event.key.keysym.sym == SDLK_ESCAPE) {
					done = 1;
				}
			}
		}

		GPU_Clear(screen);
		render_map(screen, map);
		GPU_Flip(screen);
	}
}

int main(int argc, char **argv) {
	GPU_Target *screen;
	SDL_TimerID timer_id;

	screen = GPU_Init(DISPLAY_W, DISPLAY_H, GPU_DEFAULT_INIT_FLAGS);
	if (screen == NULL) {
		fputs(SDL_GetError(), stderr);
		return 1;
	}

	SDL_EventState(SDL_MOUSEMOTION, SDL_DISABLE);

	timer_id = SDL_AddTimer(30, timer_func, NULL);

	/* Set the callback globs in the main function */
	tmx_img_load_func = SDL_tex_loader;
	tmx_img_free_func = (void (*)(void*))GPU_FreeImage;

	tmx_map *map = tmx_load(argv[1]);
	if (!map) {
		tmx_perror("Cannot load map");
		return 1;
	}

	main_loop(screen, map);

	tmx_map_free(map);

	SDL_RemoveTimer(timer_id);
	GPU_Quit();

	return 0;
}

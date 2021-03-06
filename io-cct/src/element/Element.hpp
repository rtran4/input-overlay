/**
 * Created by universal on 27.08.2018.
 * This file is part of input-overlay which is licensed
 * under the MOZILLA PUBLIC LICENSE 2.0 - mozilla.org/en-US/MPL/2.0/
 * github.com/univrsal/input-overlay
 */

#pragma once

#include "../../../io-obs/util/layout_constants.hpp"
#include <string>
#include <utility>
#include <SDL.h>

#define ELEMENT_HIDE_ALPHA  60

enum ElementError
{
    VALID,
    ID_NOT_UNIQUE,
    ID_EMPTY,
    TYPE_INVALID,
    MAPPING_EMPTY,
    KEYCODE_INVALID,
    STICK_RADIUS,
    MOUSE_RADIUS,
    TEXT_EMPTY
};

class SDL_Helper;

class Notifier;

class DialogElementSettings;

class DialogNewElement;

class CoordinateSystem;

class ccl_config;

class Texture;

/* Base class for display elements
 */
class Element
{
public:
    virtual void draw(Texture* atlas, CoordinateSystem* cs, bool selected, bool alpha) = 0;

    virtual void write_to_file(ccl_config* cfg, SDL_Point* default_dim, uint8_t& layout_flags);

    virtual SDL_Rect* get_abs_dim(CoordinateSystem* cs);

    virtual void update_settings(DialogNewElement* dialog);

    virtual void update_settings(DialogElementSettings* dialog);

    virtual ElementError is_valid(Notifier* n, SDL_Helper* h);

    void set_mapping(SDL_Rect r);

    void set_pos(int x, int y);

    void set_id(std::string id) { m_id = std::move(id); }

    void set_z_level(const uint8_t z) { m_z_level = z; }

    uint8_t get_z_level() const { return m_z_level; }

    std::string* get_id() { return &m_id; }

    int get_x() const { return m_position.x; }

    int get_y() const { return m_position.y; }

    int get_w() const { return m_mapping.w; }

    int get_h() const { return m_mapping.h; }

    int get_u() const { return m_mapping.x; }

    int get_v() const { return m_mapping.y; }

    virtual int get_vc() { return 0; }

    element_type get_type() const { return m_type; }

    SDL_Rect* get_mapping() { return &m_mapping; }

    virtual void handle_event(SDL_Event* event, SDL_Helper* helper) = 0;

    /* Creates empty element and load settings from config */
    static Element* read_from_file(ccl_config* file, const std::string& id, element_type t, SDL_Point* default_dim);

    /* Creates empty element and loads settings from dialog */
    static Element* from_dialog(DialogNewElement* dialog);

    static bool valid_type(int t);

protected:
    Element(); /* Used for creation over dialogs */
    Element(element_type t, std::string id, SDL_Point pos, uint8_t z);

    static SDL_Rect read_mapping(ccl_config* file, const std::string& id, SDL_Point* default_dim);
    static SDL_Point read_position(ccl_config* file, const std::string& id);
    static uint8_t read_layer(ccl_config* file, const std::string& id);
    static element_side read_side(ccl_config* file, const std::string& id);

    element_type m_type;
    SDL_Point m_position{}; /* Final position in overlay */
    SDL_Rect m_mapping{}; /* Texture mappings */
    SDL_Rect m_dimensions_scaled{};

    uint8_t m_scale = 0; /* Currently used scale factor */

    uint8_t m_z_level = 0; /* Determines draw and selection order */

    std::string m_id;
};

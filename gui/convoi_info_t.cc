/*
 * Copyright (c) 1997 - 2001 Hansj�rg Malthaner
 *
 * This file is part of the Simutrans project under the artistic licence.
 * (see licence.txt)
 */

#include <stdio.h>

#include "convoi_info_t.h"
#include "replace_frame.h"

#include "../simdepot.h"
#include "../vehicle/simvehikel.h"
#include "../simcolor.h"
#include "../simgraph.h"
#include "../simworld.h"
#include "../simmenu.h"
#include "../simwin.h"
#include "../convoy.h"

#include "../dataobj/fahrplan.h"
#include "../dataobj/translator.h"
#include "../dataobj/umgebung.h"
#include "../dataobj/loadsave.h"
#include "fahrplan_gui.h"
// @author hsiegeln
#include "../simlinemgmt.h"
#include "../simline.h"
#include "../boden/grund.h"
#include "messagebox.h"

#include "../utils/simstring.h"

#include "components/list_button.h"

#include "convoi_detail_t.h"

static const char cost_type[BUTTON_COUNT][64] =
{
	"Free Capacity", "Transported", "Average speed", "Comfort", "Revenue", "Operation", "Profit", "Distance", "Refunds"
#ifdef ACCELERATION_BUTTON
	, "Acceleration"
#endif
};

static const int cost_type_color[BUTTON_COUNT] =
{
	COL_FREE_CAPACITY, COL_TRANSPORTED, COL_AVERAGE_SPEED, COL_COMFORT, COL_REVENUE, COL_OPERATION, COL_PROFIT, COL_DISTANCE, COL_LIGHT_RED
#ifdef ACCELERATION_BUTTON
	, COL_YELLOW
#endif
};

static const bool cost_type_money[BUTTON_COUNT] =
{
	false, false, false, false, true, true, true, false
#ifdef ACCELERATION_BUTTON
	, false
#endif
};

karte_t *convoi_info_t::welt = NULL;

//bool convoi_info_t::route_search_in_progress=false;

/**
 * This variable defines by which column the table is sorted
 * Values:			0 = destination
 *                  1 = via
 *                  2 = via_amount
 *                  3 = amount
 *					4 = origin
 *					5 = origin_amount
 * @author prissi - amended by jamespetts (origins)
 */
const char *convoi_info_t::sort_text[SORT_MODES] = 
{
	"Zielort",
	"via",
	"via Menge",
	"Menge",
	"origin (detail)",
	"origin (amount)"
};


convoi_info_t::convoi_info_t(convoihandle_t cnv)
:	gui_frame_t(cnv->get_name(), cnv->get_besitzer()),
	scrolly(&text),
	text(" \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n"
			 " \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n"
			 " \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n"
			 " \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n"
			 " \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n \n"),
	view(cnv->front(), koord(max(64, get_base_tile_raster_width()), max(56, (get_base_tile_raster_width() * 7) / 8))),
	sort_label(translator::translate("loaded passenger/freight")),
	freight_info(8192)
{
	this->cnv = cnv;
	welt = cnv->get_welt();
	this->mean_convoi_speed = speed_to_kmh(cnv->get_akt_speed()*4);
	this->max_convoi_speed = speed_to_kmh(cnv->get_min_top_speed()*4);

	const sint16 offset_below_viewport = 21 + view.get_groesse().y;
	const sint16 total_width = 3*(BUTTON_WIDTH+BUTTON_SPACER) + 30 + view.get_groesse().x + 10;

	input.set_pos(koord(10,4));
	reset_cnv_name();
	add_komponente(&input);
	input.add_listener(this);

	add_komponente(&view);

	// this convoi doesn't belong to an AI
	button.set_groesse(koord(BUTTON_WIDTH, BUTTON_HEIGHT));
	button.set_text("Fahrplan");
	button.set_typ(button_t::roundbox);
	button.set_tooltip("Alters a schedule.");
	add_komponente(&button);
	button.set_pos(koord(BUTTON1_X,offset_below_viewport));
	button.add_listener(this);

	go_home_button.set_groesse(koord(BUTTON_WIDTH, BUTTON_HEIGHT));
	go_home_button.set_pos(koord(BUTTON2_X,offset_below_viewport));
	go_home_button.set_text("go home");
	go_home_button.set_typ(button_t::roundbox_state);
	go_home_button.set_tooltip("Sends the convoi to the last depot it departed from!");
	add_komponente(&go_home_button);
	go_home_button.add_listener(this);

	no_load_button.set_groesse(koord(BUTTON_WIDTH, BUTTON_HEIGHT));
	no_load_button.set_pos(koord(BUTTON3_X,offset_below_viewport));
	no_load_button.set_text("no load");
	no_load_button.set_typ(button_t::roundbox);
	no_load_button.set_tooltip("No goods are loaded onto this convoi.");
	add_komponente(&no_load_button);
	no_load_button.add_listener(this);

	replace_button.set_groesse(koord(BUTTON_WIDTH, BUTTON_HEIGHT));
	replace_button.set_pos(koord(BUTTON3_X,offset_below_viewport-(BUTTON_HEIGHT + 5)));
	replace_button.set_text("Replace");
	replace_button.set_typ(button_t::roundbox_state);
	replace_button.set_tooltip("Automatically replace this convoy.");
	add_komponente(&replace_button);
	replace_button.add_listener(this);

	follow_button.set_groesse(koord(view.get_groesse().x, BUTTON_HEIGHT));
	follow_button.set_text("follow me");
	follow_button.set_typ(button_t::roundbox_state);
	follow_button.set_tooltip("Follow the convoi on the map.");
	add_komponente(&follow_button);
	follow_button.add_listener(this);

	// chart
	chart.set_pos(koord(88,offset_below_viewport+BUTTON_HEIGHT+16));
	chart.set_groesse(koord(TOTAL_WIDTH-88-4, 100));
	chart.set_dimension(12, 10000);
	chart.set_visible(false);
	chart.set_background(MN_GREY1);
	chart.set_ltr(umgebung_t::left_to_right_graphs);
	int btn;
	for (btn = 0; btn < MAX_CONVOI_COST; btn++) {
		chart.add_curve( cost_type_color[btn], cnv->get_finance_history(), MAX_CONVOI_COST, btn, MAX_MONTHS, cost_type_money[btn], false, true, cost_type_money[btn]*2 );
		filterButtons[btn].init(button_t::box_state, cost_type[btn], koord(BUTTON1_X+(BUTTON_WIDTH+BUTTON_SPACER)*(btn%4), view.get_groesse().y+174+(BUTTON_HEIGHT+BUTTON_SPACER)*(btn/4)), koord(BUTTON_WIDTH, BUTTON_HEIGHT));
		filterButtons[btn].add_listener(this);
		filterButtons[btn].background = cost_type_color[btn];
		filterButtons[btn].set_visible(false);
		filterButtons[btn].pressed = false;
		if((btn == MAX_CONVOI_COST - 1) && cnv->get_line().is_bound())
		{
			continue;
		}
		else
		{
			add_komponente(filterButtons + btn);
		}
	}

#ifdef ACCELERATION_BUTTON
	//Bernd Gabriel, Sep, 24 2009: acceleration curve:
	
	for (int i = 0; i < MAX_MONTHS; i++)
	{
		physics_curves[i][0] = 0;
	}

	chart.add_curve(cost_type_color[btn], (sint64*)physics_curves, 1,0, MAX_MONTHS, cost_type_money[btn], false, true, cost_type_money[btn]*2);
	filterButtons[btn].init(button_t::box_state, cost_type[btn], koord(BUTTON1_X+(BUTTON_WIDTH+BUTTON_SPACER)*(btn%4), view.get_groesse().y+174+(BUTTON_HEIGHT+BUTTON_SPACER)*(btn/4)), koord(BUTTON_WIDTH, BUTTON_HEIGHT));
	filterButtons[btn].add_listener(this);
	filterButtons[btn].background = cost_type_color[btn];
	filterButtons[btn].set_visible(false);
	filterButtons[btn].pressed = false;
	add_komponente(filterButtons + btn);
#endif
	statistics_height = 16 + view.get_groesse().y+174+(BUTTON_HEIGHT+2)*(btn/4 + 1) - chart.get_pos().y;

	add_komponente(&chart);

	add_komponente(&sort_label);

	sort_button.set_groesse(koord(BUTTON_WIDTH, BUTTON_HEIGHT));
	sort_button.set_text(sort_text[umgebung_t::default_sortmode]);
	sort_button.set_typ(button_t::roundbox);
	sort_button.add_listener(this);
	sort_button.set_tooltip("Sort by");
	add_komponente(&sort_button);

	toggler.set_groesse(koord(BUTTON_WIDTH, BUTTON_HEIGHT));
	toggler.set_text("Chart");
	toggler.set_typ(button_t::roundbox_state);
	toggler.add_listener(this);
	toggler.set_tooltip("Show/hide statistics");
	add_komponente(&toggler);
	toggler.pressed = false;

	details_button.set_groesse(koord(BUTTON_WIDTH, BUTTON_HEIGHT));
	details_button.set_text("Details");
	details_button.set_typ(button_t::roundbox);
	details_button.add_listener(this);
	details_button.set_tooltip("Vehicle details");
	add_komponente(&details_button);

	reverse_button.set_groesse(koord(BUTTON_WIDTH*2, BUTTON_HEIGHT));
	reverse_button.set_text("reverse route");
	reverse_button.set_typ(button_t::square_state);
	reverse_button.add_listener(this);
	reverse_button.set_tooltip("When this is set, the vehicle will visit stops in reverse order.");
	reverse_button.pressed = cnv->get_reverse_schedule();
	add_komponente(&reverse_button);

	scrolly.set_pos(koord(0, offset_below_viewport+46));
	add_komponente(&scrolly);

	filled_bar.add_color_value(&cnv->get_loading_limit(), COL_YELLOW);
	filled_bar.add_color_value(&cnv->get_loading_level(), COL_GREEN);
	add_komponente(&filled_bar);

	speed_bar.set_base(max_convoi_speed);
	speed_bar.set_vertical(false);
	speed_bar.add_color_value(&mean_convoi_speed, COL_GREEN);
	add_komponente(&speed_bar);

	// we update this ourself!
	route_bar.add_color_value(&cnv_route_index, COL_GREEN);
	add_komponente(&route_bar);

	// goto line button
	line_button.init( button_t::posbutton, NULL, koord(10, 64) );
	line_button.set_targetpos( koord(0,0) );
	line_button.add_listener( this );
	line_bound = false;

	set_fenstergroesse(koord(total_width, view.get_groesse().y+222));

	cnv->set_sortby( umgebung_t::default_sortmode );

	set_min_windowsize(koord(total_width, view.get_groesse().y+138));
	set_resizemode(diagonal_resize);
	resize(koord(0,0));
}


// only handle a pending renaming ...
convoi_info_t::~convoi_info_t()
{
	// rename if necessary
	rename_cnv();
}


/**
 * komponente neu zeichnen. Die �bergebenen Werte beziehen sich auf
 * das Fenster, d.h. es sind die Bildschirkoordinaten des Fensters
 * in dem die Komponente dargestellt wird.
 *
 * Component draw again. The handed over values refer to the window,
 * i.e. there is the Bildschirkoordinaten of the window in that the
 * component is represented. (Babelfish)
 *
 * @author Hj. Malthaner
 */
void convoi_info_t::zeichnen(koord pos, koord gr)
{
	if(!cnv.is_bound() || cnv->in_depot() || cnv->get_vehikel_anzahl() == 0) 
	{
		destroy_win(this);
	}
	else {
		//Bernd Gabriel, Dec, 02 2009: common existing_convoy_t for acceleration curve and weight/speed info.
		existing_convoy_t convoy(*cnv.get_rep());

#ifdef ACCELERATION_BUTTON
		//Bernd Gabriel, Sep, 24 2009: acceleration curve:
		if (filterButtons[ACCELERATION_BUTTON].is_visible() && filterButtons[ACCELERATION_BUTTON].pressed)
		{
			const int akt_speed_soll = kmh_to_speed(convoy.calc_max_speed(convoy.get_weight_summary()));
			sint32 akt_speed = 0;
			sint32 sp_soll = 0;
			int i = MAX_MONTHS;
			physics_curves[--i][0] = akt_speed;
			while (i > 0)
			{
				convoy.calc_move(15 * 64, 1.0f, akt_speed_soll, akt_speed, sp_soll);
				physics_curves[--i][0] = speed_to_kmh(akt_speed);
			}
		}
#endif

		// Bernd Gabriel, 01.07.2009: show some colored texts and indicator
		input.set_color(cnv->has_obsolete_vehicles() ? COL_DARK_BLUE : COL_BLACK);

		if(cnv->get_besitzer()==cnv->get_welt()->get_active_player()) {
			if(  line_bound  &&  !cnv->get_line().is_bound()  ) {
				remove_komponente( &line_button );
				line_bound = false;
			}
			else if(  !line_bound  &&  cnv->get_line().is_bound()  ) {
				add_komponente( &line_button );
				line_bound = true;
			}
			button.enable();
			//go_home_button.pressed = route_search_in_progress;
			details_button.pressed = win_get_magic( magic_convoi_detail+cnv.get_id() );
			go_home_button.enable(); // Will be disabled, if convoy goes to a depot.
			if (!cnv->get_schedule()->empty()) {
				const grund_t* g = cnv->get_welt()->lookup(cnv->get_schedule()->get_current_eintrag().pos);
				if (g != NULL && g->get_depot()) {
					go_home_button.disable();
				}
				else {
					goto enable_home;
				}
			}
			else 
			{
enable_home:
				go_home_button.enable();
			}
			no_load_button.pressed = cnv->get_no_load();
			no_load_button.enable();
			replace_button.pressed = cnv->get_replace();
			replace_button.set_text(cnv->get_replace()?"Replacing":"Replace");
			replace_button.enable();
			reverse_button.pressed = cnv->get_reverse_schedule();
			reverse_button.enable();
		}
		else {
			if(  line_bound  ) {
				// do not jump to other player line window
				remove_komponente( &line_button );
				line_bound = false;
			}
			button.disable();
			go_home_button.disable();
			no_load_button.disable();
			replace_button.disable();
			reverse_button.disable();
		}
		follow_button.pressed = (cnv->get_welt()->get_follow_convoi()==cnv);

		// buffer update now only when needed by convoi itself => dedicated buffer for this
		cnv->get_freight_info(freight_info);
		text.set_text(freight_info);

		route_bar.set_base(cnv->get_route()->get_count()-1);
		cnv_route_index = cnv->front()->get_route_index() - 1;

		// all gui stuff set => display it
		gui_frame_t::zeichnen(pos, gr);

		PUSH_CLIP(pos.x+1,pos.y+16,gr.x-2,gr.y-16);

		//indicator
		{
			const int pos_x = pos.x + view.get_pos().x;
			//const int pos_y = pos.y + view.get_pos().y + 64 + 10;
			const int pos_y = pos.y + 21 + view.get_groesse().y + 8;

			COLOR_VAL color = COL_BLACK;
			switch (cnv->get_state())
			{
			case convoi_t::INITIAL: 
				color = COL_WHITE; 
				break;
			case convoi_t::WAITING_FOR_CLEARANCE_ONE_MONTH:
			case convoi_t::CAN_START_ONE_MONTH:
				color = COL_ORANGE; 
				break;
				
			default:
				if (cnv->hat_keine_route()) 
					color = COL_ORANGE;
			}
			display_ddd_box_clip(pos_x, pos_y, 64, 8, MN_GREY0, MN_GREY4);
			display_fillbox_wh_clip(pos_x + 1, pos_y + 1, 126, 6, color, true);
		}


		// convoi information
		static cbuffer_t info_buf(256);
		const int pos_x = pos.x + 11;
		const int pos_y0 = pos.y + 16 + 20;
		const char *caption = translator::translate("%s:");

		// Bernd Gabriel, Nov, 14 2009: no longer needed: //use median speed to avoid flickering
		//existing_convoy_t convoy(*cnv.get_rep());
		uint32 empty_weight = convoy.get_vehicle_summary().weight;
		uint32 gross_weight = convoy.get_weight_summary().weight;
		{
			const int pos_y = pos_y0; // line 1
			char tmp[256];
			const sint32 min_speed = convoy.calc_max_speed(convoy.get_weight_summary());
			const sint32 max_speed = convoy.calc_max_speed(weight_summary_t(empty_weight, convoy.get_current_friction()));
			sprintf(tmp, translator::translate(min_speed == max_speed ? "%i km/h (max. %ikm/h)" : "%i km/h (max. %i %s %ikm/h)"), 
				speed_to_kmh(cnv->get_akt_speed()), min_speed, translator::translate("..."), max_speed );
			display_proportional(pos_x, pos_y, tmp, ALIGN_LEFT, COL_BLACK, true );
		}

		//next important: income stuff
		{
			const int pos_y = pos_y0 + LINESPACE; // line 2
			char tmp[256];
			// Bernd Gabriel, 01.07.2009: inconsistent adding of ':'. Sometimes in code, sometimes in translation. Consistently moved to code.
			sprintf(tmp, caption, translator::translate("Gewinn"));
			int len = display_proportional(pos_x, pos_y, tmp, ALIGN_LEFT, COL_BLACK, true ) + 5;
			money_to_string(tmp, cnv->get_jahresgewinn()/100.0 );
			len += display_proportional(pos_x + len, pos_y, tmp, ALIGN_LEFT, cnv->get_jahresgewinn() > 0 ? MONEY_PLUS : MONEY_MINUS, true ) + 5;
			// Bernd Gabriel, 17.06.2009: add fixed maintenance info
			uint32 fixed_monthly = cnv->get_fixed_maintenance();
			if (fixed_monthly)
			{
				char tmp_2[64];
				tmp_2[0] = '(';
				money_to_string( tmp_2+1, cnv->get_per_kilometre_running_cost()/100.0 );
				strcat(tmp_2, translator::translate("/km)"));				
				sprintf(tmp, tmp_2, translator::translate(" %1.2f$/mon)"), fixed_monthly/100.0 );
			}
			else
			{
				//sprintf(tmp, translator::translate("(%1.2f$/km)"), cnv->get_per_kilometre_running_cost()/100.0 );
				tmp[0] = '(';
				money_to_string( tmp+1, cnv->get_per_kilometre_running_cost()/100.0 );
				strcat( tmp, "/km)" );
			}
			display_proportional(pos_x + len, pos_y, tmp, ALIGN_LEFT, cnv->has_obsolete_vehicles() ? COL_DARK_BLUE : COL_BLACK, true );
		}

		// the weight entry
		{
			const int pos_y = pos_y0 + 2 * LINESPACE; // line 3
			char tmp[256];
			// Bernd Gabriel, 01.07.2009: inconsistent adding of ':'. Sometimes in code, sometimes in translation. Consistently moved to code.
			sprintf(tmp, caption, translator::translate("Gewicht"));
			int len = display_proportional(pos_x, pos_y, tmp, ALIGN_LEFT, COL_BLACK, true ) + 5;
			int freight_weight = gross_weight - empty_weight; // cnv->get_sum_gesamtgewicht() - cnv->get_sum_gewicht();
			sprintf(tmp, translator::translate(freight_weight ? "%g (%g) t" : "%g t"), gross_weight * 0.001f, freight_weight * 0.001f);
			display_proportional(pos_x + len, pos_y, tmp, ALIGN_LEFT, 
				cnv->get_overcrowded() > 0 ? COL_DARK_PURPLE : // overcrowded
				!cnv->get_finance_history(0, CONVOI_TRANSPORTED_GOODS) && !cnv->get_finance_history(1, CONVOI_TRANSPORTED_GOODS) ? COL_YELLOW : // nothing moved in this and past month
				COL_BLACK, true );
		}

		{
			const int pos_y = pos_y0 + 3 * LINESPACE; // line 4
			// next stop
			char tmp[256];
			// Bernd Gabriel, 01.07.2009: inconsistent adding of ':'. Sometimes in code, sometimes in translation. Consistently moved to code.
			sprintf(tmp, caption, translator::translate("Fahrtziel"));
			int len = display_proportional(pos_x, pos_y, tmp, ALIGN_LEFT, COL_BLACK, true ) + 5;
			info_buf.clear();
			const schedule_t *fpl = cnv->get_schedule();
			fahrplan_gui_t::gimme_short_stop_name(info_buf, cnv->get_welt(), cnv->get_besitzer(), fpl, fpl->get_aktuell(), 34);
			len += display_proportional_clip(pos_x + len, pos_y, info_buf, ALIGN_LEFT, COL_BLACK, true ) + 5;

			// convoi load indicator
			const int bar_x = max(11 + len, BUTTON3_X);
			route_bar.set_pos(koord(bar_x, 22 + 3 * LINESPACE));
			route_bar.set_groesse(koord(view.get_pos().x - bar_x - 5, 4));
		}

		/*
		 * only show assigned line, if there is one!
		 * @author hsiegeln
		 */
		if(  cnv->get_line().is_bound()  ) {
			const int pos_y = pos_y0 + 4 * LINESPACE; // line 5
			const int line_x = pos_x + line_bound * 12;
			char tmp[256];
			// Bernd Gabriel, 01.07.2009: inconsistent adding of ':'. Sometimes in code, sometimes in translation. Consistently moved to code.
			sprintf(tmp, caption, translator::translate("Serves Line"));
			int len = display_proportional(line_x, pos_y, tmp, ALIGN_LEFT, COL_BLACK, true ) + 5;
			display_proportional_clip(line_x + len, pos_y, cnv->get_line()->get_name(), ALIGN_LEFT, cnv->get_line()->get_state_color(), true );
		}
		POP_CLIP();
	}
}


// activate the statistic
void convoi_info_t::show_hide_statistics( bool show )
{
	toggler.pressed = show;
	const koord offset = show ? koord(0, 170) : koord(0, -170);
	set_min_windowsize(get_min_windowsize() + offset);
	scrolly.set_pos(scrolly.get_pos() + offset);
	chart.set_visible(show);
	set_fenstergroesse(get_fenstergroesse() + offset);
	resize(koord(0,0));
	for (int i=0;i<MAX_CONVOI_COST;i++) {
		filterButtons[i].set_visible(toggler.pressed);
	}
}


/**
 * This method is called if an action is triggered
 * @author Hj. Malthaner
 */
bool convoi_info_t::action_triggered( gui_action_creator_t *komp,value_t /* */)
{
	// follow convoi on map?
	if(komp == &follow_button) {
		if(cnv->get_welt()->get_follow_convoi()==cnv) {
			// stop following
			cnv->get_welt()->set_follow_convoi( convoihandle_t() );
		}
		else {
			cnv->get_welt()->set_follow_convoi(cnv);
		}
		return true;
	}

	// details?
	if(komp == &details_button) {
		create_win(20, 20, new convoi_detail_t(cnv), w_info, magic_convoi_detail+cnv.get_id() );
		return true;
	}

	if(  komp == &line_button  ) {
		cnv->get_besitzer()->simlinemgmt.show_lineinfo( cnv->get_besitzer(), cnv->get_line() );
		cnv->get_welt()->set_dirty();
	}

	if(  komp == &input  ) {
		// rename if necessary
		rename_cnv();
	}

	// sort by what
	if(komp == &sort_button) {
		// sort by what
		umgebung_t::default_sortmode = (sort_mode_t)((int)(cnv->get_sortby()+1)%(int)SORT_MODES);
		sort_button.set_text(sort_text[umgebung_t::default_sortmode]);
		cnv->set_sortby( umgebung_t::default_sortmode );
	}

	// some actions only allowed, when I am the player
	if(cnv->get_besitzer()==cnv->get_welt()->get_active_player()) {

		if(komp == &button) {
			cnv->call_convoi_tool( 'f', NULL );
			return true;
		}

		//if(komp == &no_load_button    &&    !route_search_in_progress) {
		if(komp == &no_load_button) {
			cnv->call_convoi_tool( 'n', NULL );
			return true;
		}

		if(komp == &replace_button) 
		{
			create_win(20, 20, new replace_frame_t(cnv, get_name()), w_info, magic_replace + cnv.get_id() );
			return true;
		}

		if(komp == &go_home_button) 
		{
			// limit update to certain states that are considered to be save for fahrplan updates
			int state = cnv->get_state();
			if(state==convoi_t::FAHRPLANEINGABE) 
			{
				DBG_MESSAGE("convoi_info_t::action_triggered()","convoi state %i => cannot change schedule ... ", state );
				return false;
			}
			go_home_button.pressed = true;
			cnv->call_convoi_tool('P', NULL);
			go_home_button.pressed = false;
			return true;
		} // end go home button

		if(komp == &reverse_button)
		{
			cnv->call_convoi_tool('V', NULL);
			reverse_button.pressed = !reverse_button.pressed;
		}
	}

	if (komp == &toggler) {
		show_hide_statistics( toggler.pressed^1 );
		return true;
	}

	for ( int i = 0; i<BUTTON_COUNT; i++) {
		if (komp == &filterButtons[i]) {
			filterButtons[i].pressed = !filterButtons[i].pressed;
			if(filterButtons[i].pressed) {
				chart.show_curve(i);
			}
			else {
				chart.hide_curve(i);
			}

			return true;
		}
	}

	return false;
}


void convoi_info_t::reset_cnv_name()
{
	// change text input of selected line
	if (cnv.is_bound()) {
		tstrncpy(old_cnv_name, cnv->get_name(), sizeof(old_cnv_name));
		tstrncpy(cnv_name, cnv->get_name(), sizeof(cnv_name));
		input.set_text(cnv_name, sizeof(cnv_name));
	}
}


void convoi_info_t::rename_cnv()
{
	if (cnv.is_bound()) {
		const char *t = input.get_text();
		// only change if old name and current name are the same
		// otherwise some unintended undo if renaming would occur
		if(  t  &&  t[0]  &&  strcmp(t, cnv->get_name())  &&  strcmp(old_cnv_name, cnv->get_name())==0) {
			// text changed => call tool
			cbuffer_t buf(300);
			buf.printf( "c%u,%s", cnv.get_id(), t );
			werkzeug_t *w = create_tool( WKZ_RENAME_TOOL | SIMPLE_TOOL );
			w->set_default_param( buf );
			cnv->get_welt()->set_werkzeug( w, cnv->get_besitzer());
			// since init always returns false, it is save to delete immediately
			delete w;
			// do not trigger this command again
			tstrncpy(old_cnv_name, t, sizeof(old_cnv_name));
		}
	}
}


/**
 * Resize the contents of the window
 * @author Markus Weber
 */
void convoi_info_t::resize(const koord delta)
{
	gui_frame_t::resize(delta);

	input.set_groesse(koord(get_fenstergroesse().x - 20, 13));

	view.set_pos(koord(get_fenstergroesse().x - view.get_groesse().x - 10 , 21));
	follow_button.set_pos(koord(view.get_pos().x, view.get_groesse().y + 21));

	scrolly.set_groesse(get_client_windowsize()-scrolly.get_pos());

	const sint16 yoff = scrolly.get_pos().y-BUTTON_HEIGHT-2;
	sort_button.set_pos(koord(BUTTON1_X,yoff));
	toggler.set_pos(koord(BUTTON3_X,yoff));
	details_button.set_pos(koord(BUTTON4_X,yoff));
	sort_label.set_pos(koord(BUTTON1_X,yoff-LINESPACE));
	reverse_button.set_pos(koord(BUTTON3_X,yoff-BUTTON_HEIGHT));

	// convoi speed indicator
	speed_bar.set_pos(koord(BUTTON3_X,22+0*LINESPACE));
	speed_bar.set_groesse(koord(view.get_pos().x - BUTTON3_X - 5, 4));

	// convoi load indicator
	filled_bar.set_pos(koord(BUTTON3_X,22+2*LINESPACE));
	filled_bar.set_groesse(koord(view.get_pos().x - BUTTON3_X - 5, 4));
}



convoi_info_t::convoi_info_t(karte_t *welt)
:	gui_frame_t("", NULL),
	scrolly(&text),
	text(""),
	view( welt, koord(64,64)),
	sort_label(translator::translate("loaded passenger/freight")),
	freight_info(0)
{
	this->welt = welt;
}



void convoi_info_t::rdwr(loadsave_t *file)
{
	koord3d cnv_pos;
	char name[128];
	koord gr = get_fenstergroesse();
	uint32 flags = 0;
	bool stats = toggler.pressed;
	sint32 xoff = scrolly.get_scroll_x();
	sint32 yoff = scrolly.get_scroll_y();
	if(  file->is_saving()  ) {
		cnv_pos = cnv->front()->get_pos();
		for( int i = 0; i<MAX_CONVOI_COST; i++) {
			if(  filterButtons[i].pressed  ) {
				flags |= (1<<i);
			}
		}
		tstrncpy( name, cnv->get_name(), 128 );
	}
	cnv_pos.rdwr( file );
	file->rdwr_str( name, lengthof(name) );
	gr.rdwr( file );
	file->rdwr_long( flags );
	file->rdwr_byte( umgebung_t::default_sortmode );
	file->rdwr_bool( stats );
	file->rdwr_long( xoff );
	file->rdwr_long( yoff );
	if(  file->is_loading()  ) {
		// find convoi by name and position
		if(  grund_t *gr = welt->lookup(cnv_pos)  ) {
			for(  uint8 i=0;  i<gr->get_top();  i++  ) {
				if(  gr->obj_bei(i)->is_moving()  ) {
					vehikel_t const* const v = ding_cast<vehikel_t>(gr->obj_bei(i));
					if(  v  &&  v->get_convoi()  ) {
						if(  strcmp(v->get_convoi()->get_name(),name)==0  ) {
							cnv = v->get_convoi()->self;
							break;
						}
					}
				}
			}
		}
		// we might be unlucky, then search all convois for a convoi with this name
		if(  !cnv.is_bound()  ) {
			for(  vector_tpl<convoihandle_t>::const_iterator i = welt->convois_begin(), end = welt->convois_end();  i != end;  ++i  ) {
				if(  strcmp( (*i)->get_name(),name)==0  ) {
					cnv = *i;
					break;
				}
			}
		}
		// still not found?
		if(  !cnv.is_bound()  ) {
			dbg->error( "convoi_info_t::rdwr()", "Could not restore convoi info window of %s", name );
			destroy_win( this );
			return;
		}
		// now we can open the window ...
		KOORD_VAL xpos = win_get_posx( this );
		KOORD_VAL ypos = win_get_posy( this );
		convoi_info_t *w = new convoi_info_t(cnv);
		create_win( xpos, ypos, w, w_info, magic_convoi_info+cnv.get_id() );
		if(  stats  ) {
			gr.y -= 170;
		}
		w->set_fenstergroesse( gr );
		for( int i = 0; i<MAX_CONVOI_COST; i++) {
			w->filterButtons[i].pressed = (flags>>i)&1;
			if(w->filterButtons[i].pressed) {
				w->chart.show_curve(i);
			}
		}
		if(  stats  ) {
			w->show_hide_statistics( true );
		}
		cnv->get_freight_info(w->freight_info);
		w->text.set_text(w->freight_info);
		w->text.recalc_size();
		w->scrolly.set_scroll_position( xoff, yoff );
		// we must invalidate halthandle
		cnv = convoihandle_t();
		destroy_win( this );
	}
}

#include "utils/simstring.h"
#include "dataobj/translator.h"
#include "dataobj/loadsave.h"
#include "simtypes.h"
#include "simline.h"
#include "simhalt.h"
#include "player/simplay.h"
#include "vehicle/simvehikel.h"
#include "simconvoi.h"
#include "convoihandle_t.h"
#include "simworld.h"
#include "simlinemgmt.h"

uint8 simline_t::convoi_to_line_catgory[MAX_CONVOI_COST]={LINE_CAPACITY, LINE_TRANSPORTED_GOODS, LINE_AVERAGE_SPEED, LINE_COMFORT, LINE_REVENUE, LINE_OPERATIONS, LINE_PROFIT, LINE_DISTANCE, LINE_REFUNDS };

karte_t *simline_t::welt=NULL;


simline_t::simline_t(karte_t* welt, spieler_t* sp)
{
	self = linehandle_t(this);
	char printname[128];
	sprintf( printname, "(%i) %s", self.get_id(), translator::translate("Line",welt->get_einstellungen()->get_name_language_id()) );
	name = printname;
	init_financial_history();
	this->id = INVALID_LINE_ID;
	this->welt = welt;
	this->old_fpl = NULL;
	this->fpl = NULL;
	this->sp = sp;
	withdraw = false;
	state_color = COL_YELLOW;
	for(uint8 i = 0; i < MAX_LINE_COST; i ++)
	{	
		rolling_average[i] = 0;
		rolling_average_count[i] = 0;
	}
	start_reversed = false;
}



void simline_t::set_line_id(uint32 id)
{
	this->id = id;
	char printname[128];
	sprintf( printname, "(%i) %s", self.get_id(), translator::translate("Line",welt->get_einstellungen()->get_name_language_id()) );
	name = printname;
}



simline_t::~simline_t()
{
	DBG_DEBUG("simline_t::~simline_t()", "deleting fpl=%p and old_fpl=%p", fpl, old_fpl);

	assert(count_convoys()==0);
	unregister_stops();

	delete fpl;
	delete old_fpl;
	self.detach();

	DBG_MESSAGE("simline_t::~simline_t()", "line %d (%p) destroyed", id, this);
}



void simline_t::add_convoy(convoihandle_t cnv)
{
	if (line_managed_convoys.empty()) {
		// first convoi -> ok, now we can announce this connection to the stations
		register_stops(fpl);
	}

	// first convoi may change line type
	if (type == trainline  &&  line_managed_convoys.empty() &&  cnv.is_bound()) {
		// check, if needed to convert to tram/monorail line
		if (vehikel_t const* const v = cnv->front()) {
			switch (v->get_besch()->get_waytype()) {
				case tram_wt:     type = simline_t::tramline;     break;
				// elevated monorail were saved with wrong coordinates for some versions.
				// We try to recover here
				case monorail_wt: type = simline_t::monorailline; break;
				default:          break;
			}
		}
	}
	// only add convoy if not already member of line
	line_managed_convoys.append_unique(cnv);

	// what goods can this line transport?
	bool update_schedules = false;
	if(  cnv->get_state()!=convoi_t::INITIAL  ) {
		/*
		// already on the road => need to add them
		for(  uint8 i=0;  i<cnv->get_vehikel_anzahl();  i++  ) {
			// Only consider vehicles that really transport something
			// this helps against routing errors through passenger
			// trains pulling only freight wagons
			if(  cnv->get_vehikel(i)->get_fracht_max() == 0  ) {
				continue;
			}
			const ware_besch_t *ware=cnv->get_vehikel(i)->get_fracht_typ();
			if(  ware!=warenbauer_t::nichts  &&  !goods_catg_index.is_contained(ware->get_catg_index())  ) {
				goods_catg_index.append( ware->get_catg_index(), 1 );
				update_schedules = true;
			}
		}
		*/

		// Added by : Knightly
		const minivec_tpl<uint8> &categories = cnv->get_goods_catg_index();
		const uint8 catg_count = categories.get_count();
		for (uint8 i = 0; i < catg_count; i++)
		{
			if (!goods_catg_index.is_contained(categories[i]))
			{
				goods_catg_index.append(categories[i], 1);
				update_schedules = true;
			}
		}
	}

	// will not hurt ...
	financial_history[0][LINE_CONVOIS] = count_convoys();
	recalc_status();

	// do we need to tell the world about our new schedule?
	if(  update_schedules  ) {

		// Added by : Knightly
		haltestelle_t::refresh_routing(fpl, goods_catg_index, sp, welt->get_einstellungen()->get_default_path_option());
	}

	// if the schedule is flagged as bidirectional, set the initial convoy direction
	if( fpl->is_bidirectional() ) {
		cnv->set_reverse_schedule(start_reversed);
		start_reversed = !start_reversed;
	}
}



void simline_t::remove_convoy(convoihandle_t cnv)
{
	if(line_managed_convoys.is_contained(cnv)) {
		line_managed_convoys.remove(cnv);
		recalc_catg_index();
		financial_history[0][LINE_CONVOIS] = count_convoys();
		recalc_status();
	}
	if(line_managed_convoys.empty()) {
		unregister_stops();
	}
}



void simline_t::rdwr(loadsave_t *file)
{
	xml_tag_t s( file, "simline_t" );

	assert(fpl);

	if(  file->is_loading()  ) {
		char name[1024];
		file->rdwr_str(name, lengthof(name));
		this->name = name;
	}
	else {
		char name[1024];
		tstrncpy( name, this->name.c_str(), lengthof(name) );
		file->rdwr_str(name, lengthof(name));
	}

	if(file->get_version()<88003) {
		sint32 dummy=id;
		file->rdwr_long(dummy);
		id = (uint16)dummy;
	}
	else {
		file->rdwr_short(id);
	}
	fpl->rdwr(file);

	//financial history

	if(file->get_version() < 102002 || (file->get_version() < 103000 && file->get_experimental_version() < 7))
	{
		for (int j = 0; j<LINE_DISTANCE; j++) 
		{
			for (int k = MAX_MONTHS-1; k>=0; k--) 
			{
				if(((j == LINE_AVERAGE_SPEED || j == LINE_COMFORT) && file->get_experimental_version() <= 1) || (j == LINE_REFUNDS && file->get_experimental_version() < 8))
				{
					// Versions of Experimental saves with 1 and below
					// did not have settings for average speed or comfort.
					// Thus, this value must be skipped properly to
					// assign the values. Likewise, versions of Experimental < 8
					// did not store refund information.
					if(file->is_loading())
					{
						financial_history[k][j] = 0;
					}
					continue;
				}
				file->rdwr_longlong(financial_history[k][j]);

			}
		}
		for (int k = MAX_MONTHS-1; k>=0; k--) 
		{
			financial_history[k][LINE_DISTANCE] = 0;
		}
	}
	else 
	{
		for (int j = 0; j<MAX_LINE_COST; j++) 
		{
			for (int k = MAX_MONTHS-1; k>=0; k--)
			{
				if(((j == LINE_AVERAGE_SPEED || j == LINE_COMFORT) && file->get_experimental_version() <= 1) || (j == LINE_REFUNDS && file->get_experimental_version() < 8))
				{
					// Versions of Experimental saves with 1 and below
					// did not have settings for average speed or comfort.
					// Thus, this value must be skipped properly to
					// assign the values. Likewise, versions of Experimental < 8
					// did not store refund information.
					if(file->is_loading())
					{
						financial_history[k][j] = 0;
					}
					continue;
				}
				file->rdwr_longlong(financial_history[k][j]);
			}
		}
	}

	if(file->get_version()>=102002) {
		file->rdwr_bool(withdraw);
	}

	if(file->get_experimental_version() >= 9) 
	{
		file->rdwr_bool( start_reversed);
	}

	// otherwise inintialized to zero if loading ...
	financial_history[0][LINE_CONVOIS] = count_convoys();

	if(file->get_experimental_version() >= 2)
	{
		const uint8 counter = file->get_version() < 103000 ? LINE_DISTANCE : MAX_LINE_COST;
		for(uint8 i = 0; i < counter; i ++)
		{	
			file->rdwr_long(rolling_average[i]);
			if (file->get_experimental_version() >= 10)
			{
				file->rdwr_long(rolling_average_count[i]);
			}
			else
			{
				// Member size lengthened in version 10
				uint16 rolling_average_count = rolling_average_count[i];
				file->rdwr_short(rolling_average_count);
				if(file->is_loading()) {
					rolling_average_count[i] = rolling_average_count;
				}
			}
		}
	}
}



void simline_t::laden_abschliessen()
{
	if(  line_managed_convoys.get_count()>0  ) {
		register_stops(fpl);
	}
	recalc_status();
}



void simline_t::register_stops(schedule_t * fpl)
{
DBG_DEBUG("simline_t::register_stops()", "%d fpl entries in schedule %p", fpl->get_count(),fpl);
	for (int i = 0; i<fpl->get_count(); i++) {
		const halthandle_t halt = haltestelle_t::get_halt( welt, fpl->eintrag[i].pos, sp );
		if(halt.is_bound()) {
//DBG_DEBUG("simline_t::register_stops()", "halt not null");
			halt->add_line(self);
		}
		else {
DBG_DEBUG("simline_t::register_stops()", "halt null");
		}
	}
}

int simline_t::get_replacing_convoys_count() const {
	int count=0;
	for (int i=0; i<line_managed_convoys.get_count(); ++i) {
		if (line_managed_convoys[i]->get_replace()) {
			count++;
		}
	}
	return count;
}

void simline_t::unregister_stops()
{
	unregister_stops(fpl);
}



void simline_t::unregister_stops(schedule_t * fpl)
{
	for (int i = 0; i<fpl->get_count(); i++) {
		halthandle_t halt = haltestelle_t::get_halt( welt, fpl->eintrag[i].pos, sp );
		if(halt.is_bound()) {
			halt->remove_line(self);
		}
	}
}



void simline_t::renew_stops()
{
	if(  line_managed_convoys.get_count()>0  ) 
	{
		if(  old_fpl  ) 
		{
			unregister_stops( old_fpl );

			// Added by : Knightly
			haltestelle_t::refresh_routing(old_fpl, goods_catg_index, sp, 0);
		}
		register_stops( fpl );
	
		// Added by Knightly
		haltestelle_t::refresh_routing(fpl, goods_catg_index, sp, welt->get_einstellungen()->get_default_path_option());
		
		DBG_DEBUG("simline_t::renew_stops()", "Line id=%d, name='%s'", id, name.c_str());
	}
}



void simline_t::new_month()
{
	recalc_status();
	for (int j = 0; j<MAX_LINE_COST; j++) {
		for (int k = MAX_MONTHS-1; k>0; k--) {
			financial_history[k][j] = financial_history[k-1][j];
		}
		financial_history[0][j] = 0;
	}
	financial_history[0][LINE_CONVOIS] = count_convoys();

	for(uint8 i = 0; i < MAX_LINE_COST; i ++)
	{	
		rolling_average[i] = 0;
		rolling_average_count[i] = 0;
	}
}



/*
 * called from line_management_gui.cc to prepare line for a change of its schedule
 */
void simline_t::prepare_for_update()
{
	DBG_DEBUG("simline_t::prepare_for_update()", "line %d (%p)", id, this);
	delete old_fpl;
	old_fpl = fpl->copy();
}



void simline_t::init_financial_history()
{
	MEMZERO(financial_history);
}



/*
 * the current state saved as color
 * Meanings are BLACK (ok), WHITE (no convois), YELLOW (no vehicle moved), RED (last month income minus), DARK PURPLE (some vehicles overcrowded), BLUE (at least one convoi vehicle is obsolete)
 */
void simline_t::recalc_status()
{
	// normal state
	// Moved from an else statement at bottom
	// to ensure that this value is always initialised.
	state_color = COL_BLACK;

	if(financial_history[0][LINE_CONVOIS]==0) 
	{
		// no convoys assigned to this line
		state_color = COL_WHITE;
		withdraw = false;
	}
	else if(financial_history[0][LINE_PROFIT]<0) 
	{
		// Loss-making
		state_color = COL_RED;
	} 

	else if((financial_history[0][LINE_OPERATIONS]|financial_history[1][LINE_OPERATIONS])==0) 
	{
		// Stuck or static
		state_color = COL_YELLOW;
	}
	else if(has_overcrowded())
	{
		// Overcrowded
		state_color = COL_DARK_PURPLE;
	}
	else if(welt->use_timeline()) 
	{
		// Has obsolete vehicles.
		bool has_obsolete = false;
		for(unsigned i=0;  !has_obsolete  &&  i<line_managed_convoys.get_count();  i++ ) 
		{
			has_obsolete = line_managed_convoys[i]->has_obsolete_vehicles();
		}
		// now we have to set it
		state_color = has_obsolete ? COL_DARK_BLUE : COL_BLACK;
	}
}

bool simline_t::has_overcrowded() const
{
	ITERATE(line_managed_convoys,i)
	{
		if(line_managed_convoys[i]->get_overcrowded() > 0)
		{
			return true;
		}
	}
	return false;
}


// recalc what good this line is moving
void simline_t::recalc_catg_index()
{
	// first copy old
	minivec_tpl<uint8> old_goods_catg_index(goods_catg_index.get_count());
	for(  uint i=0;  i<goods_catg_index.get_count();  i++  ) {
		old_goods_catg_index.append( goods_catg_index[i] );
	}
	goods_catg_index.clear();
	withdraw = line_managed_convoys.get_count()>0;
	// then recreate current
	for(unsigned i=0;  i<line_managed_convoys.get_count();  i++ ) {
		// what goods can this line transport?
		// const convoihandle_t cnv = line_managed_convoys[i];
		const convoi_t *cnv = line_managed_convoys[i].get_rep();
		withdraw &= cnv->get_withdraw();

		const minivec_tpl<uint8> &convoys_goods = cnv->get_goods_catg_index();
		for(  uint8 i = 0;  i < convoys_goods.get_count();  i++  ) {
			const uint8 catg_index = convoys_goods[i];
			goods_catg_index.append_unique( catg_index, 1 );
		}
	}
	
	// Modified by	: Knightly
	// Purpose		: Determine removed and added categories and refresh only those categories.
	//				  Avoids refreshing unchanged categories
	minivec_tpl<uint8> differences(goods_catg_index.get_count() + old_goods_catg_index.get_count());

	// removed categories : present in old category list but not in new category list
	for (uint8 i = 0; i < old_goods_catg_index.get_count(); i++)
	{
		if ( ! goods_catg_index.is_contained( old_goods_catg_index[i] ) )
		{
			differences.append( old_goods_catg_index[i] );
		}
	}

	// added categories : present in new category list but not in old category list
	for (uint8 i = 0; i < goods_catg_index.get_count(); i++)
	{
		if ( ! old_goods_catg_index.is_contained( goods_catg_index[i] ) )
		{
			differences.append( goods_catg_index[i] );
		}
	}

	// refresh only those categories which are either removed or added to the category list
	haltestelle_t::refresh_routing(fpl, differences, sp, welt->get_einstellungen()->get_default_path_option());
}



void simline_t::set_withdraw( bool yes_no )
{
	withdraw = yes_no  &&  (line_managed_convoys.get_count()>0);
	// convois in depots will be immeadiately destroyed, thus we go backwards
	for( sint32 i=line_managed_convoys.get_count()-1;  i>=0;  i--  ) {
		line_managed_convoys[i]->set_no_load(yes_no);	// must be first, since set withdraw might destroy convoi if in depot!
		line_managed_convoys[i]->set_withdraw(yes_no);
	}
}


// Added by : Knightly
bool simline_t::is_schedule_updated() const
{
	if (!fpl)
		return false;
	else if (!old_fpl)
		return true;
	else  // Case : Both fpl and old_fpl contains a schedule
		return !old_fpl->matches(welt, fpl);
}

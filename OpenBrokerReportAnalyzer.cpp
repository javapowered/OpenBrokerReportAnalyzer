#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include "boost/date_time/posix_time/posix_time.hpp"

using boost::property_tree::ptree; 
namespace pt = boost::property_tree;

using namespace boost::gregorian;
using namespace boost::posix_time;

// empty ptree helper
const pt::ptree& empty_ptree()
{
	static pt::ptree t;
	return t;
}

double my_stod(const std::string &valueAsString) {
	std::istringstream totalSString( valueAsString );
	double valueAsDouble;
	// maybe use some manipulators
	totalSString >> valueAsDouble;
	if(!totalSString)
		throw std::runtime_error("Error converting to double");    
	return valueAsDouble;
}

struct stats {
	stats() : stocks(0), profit(0), lastPrice(0), commision(0) {

	}

	int stocks;
	double profit;
	double lastPrice;
	double commision;
};

std::unordered_map<std::string, stats> ticker2stats;
std::unordered_map<std::string, std::vector<stats>> ticker2AllStats;
std::unordered_map<std::string, date> ticker2lastDealDate;

double totalSpotCommision = 0;
double totalFortsCommision = 0;

boost::property_tree::ptree pt_spot;
boost::property_tree::ptree pt_forts;

// "from" - including
// "to"   - excluding
// so if from == to - nothing
void finishSecurityStats(std::string& security_name, date& from, date& to)
{
	day_iterator dayIt(from);
	if (dayIt > to) {
		std::cout << "Error, dayIt > to, exiting! " <<  to_simple_string(*dayIt) << " > " << to << std::endl;
		exit(0);
	}
	std::vector<stats>& allStats = ticker2AllStats[security_name];
	while (dayIt < to) {
		stats statsCopy = ticker2stats[security_name];
		allStats.push_back(statsCopy);
		// std::cout << "finish stats for " << security_name << " date = " << to_simple_string(*dayIt) << std::endl;
		++dayIt;
		// std::cout << "check!! new dayIt " <<  to_simple_string(*dayIt) << " collection = " << ticker2lastDealDate[security_name] << std::endl;
	}
}

date parseDate(const std::string& s) {
	std::stringstream myStream(s);
	int year;
	int month;
	int day;
	// 2014-01-01T00:00:00
	myStream >> year; myStream.ignore();
	myStream >> month; myStream.ignore();
	myStream >> day; myStream.ignore();
	return date(year, month, day);
}
//////////////////////////////////////////////////////////////////////////////
// ovazhnev@gmail.com
//
// В личном кабинете открытия я выбираю конкретный счёт и субсчёт,
// работает ли на "все счета" не проверял
//
// parameters: t+n report file name, forts report filename
// Example:
//   OpenBrokerReportAnalyzer.exe broker_report_20140101_20140219_t_n.xml broker_report_20140101_20140219_forts.xml

// TODO: по каждому дню по каждой акции надо сохранять изменение закрытие к открытию
// чтобы понять резки заработки из за того что хорошо наторговал или из за того что в позиции завис
int main(int argc, char* argv[])
{
	using namespace boost::gregorian;
	using namespace boost::posix_time;

	setlocale(LC_ALL, "Russian");

	const int nGroups = 3;
	const int groupSize = 3;
	std::string groups[nGroups][groupSize] = 
	{
		{"LKOH-6.14",  "LKOH-9.14", "LKOH-12.14"}, {"RTS-6.14",   "RTS-9.14", "RTS-12.14"}, {"SBRF-6.14",  "SBRF-9.14", "SBRF-12.14"}
	};

	boost::property_tree::xml_parser::read_xml(argv[1], pt_spot);
	boost::property_tree::xml_parser::read_xml(argv[2], pt_forts);

	date date_from;
	date date_to;
	totalSpotCommision = 0;
	totalFortsCommision = 0;
	for (auto const& rootNode : pt_spot)
	{
		const pt::ptree& rootAttributes = rootNode.second.get_child("<xmlattr>", empty_ptree());
		for (auto const& rootAttr : rootAttributes) {
			const std::string& attrName = rootAttr.first;
			const std::string& attrVal = rootAttr.second.data();
			// std::cout << attrName << " = " << attrVal << std::endl;
			if (attrName == "date_from") {
				date_from = parseDate(attrVal);
			}
			if (attrName == "date_to") {
				date_to = parseDate(attrVal);
			}
		}
		for (auto const& rootNode2 : rootNode.second)
		{
			if (rootNode2.first == "spot_main_deals_conclusion") {
				for (auto const& itemNode : rootNode2.second) {
					const pt::ptree& dealAttributes = itemNode.second.get_child("<xmlattr>", empty_ptree());
					std::string security_name;
					int stocks;
					double price;
					double broker_commission;
					date dealDate;
					bool skipItem = false;
					for (auto const& dealAttr : dealAttributes)
					{
						const std::string& attrName = dealAttr.first;
						const std::string& attrVal = dealAttr.second.data();
						// std::cout << attrName << " = " << attrVal << std::endl;
						if (attrName == "security_name") {
							security_name = attrVal;
						}
						if (attrName == "buy_qnty") {
							stocks = stoi(attrVal);
						}
						if (attrName == "sell_qnty") {
							stocks = -stoi(attrVal);
						}
						if (attrName == "price") {
							// price = std::stod(attrVal);
							price = my_stod(attrVal);
							// std::cout << "price converted! " << price << " " << attrVal << std::endl;
						}
						if (attrName == "broker_commission") {
							//broker_commission = std::stod(attrVal);
							broker_commission = my_stod(attrVal);
							// std::cout << "broker_commission converted! " << broker_commission <<  " " << attrVal << std::endl;
						}
						if (attrName == "conclusion_date") {
							dealDate = parseDate(attrVal);
						}
						if (attrName == "comment") {
							// не надо анализировать
							// comment="РПС, перенос позиции
							if (attrVal.length() > 0) {
								skipItem = true;
								break;
							}
						}
					}
					if (skipItem) {
						continue;
					}
					// std::cout << std::endl;
					// std::cout << security_name << " " << stocks << " " << price << " " << broker_commission << " " << dealDate << std::endl << std::endl;

					// need finish security stats before updating stats
					// for example if old deal from 18 Jan and new deal from 19 Jan
					// we should first store stats for 18 Jan and then count deal for 19 Jan
					auto lastDealDateIt = ticker2lastDealDate.find(security_name);
					if (lastDealDateIt == ticker2lastDealDate.end()) {
						finishSecurityStats(security_name, date_from, dealDate);
						ticker2lastDealDate[security_name] = dealDate;
					} else {
						date lastDealDate = lastDealDateIt->second;
						finishSecurityStats(security_name, lastDealDate, dealDate);
						ticker2lastDealDate[security_name] = dealDate;
					}

					stats& stats = ticker2stats[security_name];
					stats.stocks += stocks;
					stats.profit += -stocks * price - broker_commission;
					stats.lastPrice = price;
					stats.commision += broker_commission;
					totalSpotCommision += broker_commission;

					/*std::cout << security_name << " stats stocks = " << stats.stocks << " stats.profit = "
						<< stats.profit << " stats.lastPrice = " << stats.lastPrice << " stats.commision = "  << stats.commision << std::endl;*/
				}
			}
		}
	}

	for (auto const& rootNode : pt_forts)
	{
		for (auto const& rootNode2 : rootNode.second)
		{
			if (rootNode2.first == "common_deal") {
				for (auto const& itemNode : rootNode2.second) {
					const pt::ptree& dealAttributes = itemNode.second.get_child("<xmlattr>", empty_ptree());
					std::string security_name;
					int stocks;
					double price;
					double commission;
					date dealDate;
					for (auto const& dealAttr : dealAttributes)
					{
						const std::string& attrName = dealAttr.first;
						const std::string& attrVal = dealAttr.second.data();
						// std::cout << attrName << " = " << attrVal << std::endl;
						if (attrName == "security_code") {
							security_name = attrVal;
						}
						if (attrName == "quantity") {
							stocks = stoi(attrVal);
						}
						if (attrName == "deal_symbol") {
							// полагаем что в репорте поле deal_symbol идёт после quantity
							if (stocks == 0) {
								std::cout << "Error stocks = 0!" << std::endl;
								return 0;
							}
							if (attrVal[0] == 'S') {
								stocks = -stocks;
							}
						}
						if (attrName == "price_rur") {
							price = my_stod(attrVal);
						}
						if (attrName == "comm_stock") {
							commission = my_stod(attrVal);
						}
						if (attrName == "deal_date") {
							dealDate = parseDate(attrVal);
						}
	
					}
					// std::cout << security_name << " " << stocks << " " << price << " " << commission << " " << dealDate << std::endl << std::endl;

					// небольшой хак. возможно чё-то попало с вечерки предыдущего дня
					// просто считаем что эти сделки прошли в date_from
					if (dealDate < date_from) {
						dealDate = date_from;
					}
					auto lastDealDateIt = ticker2lastDealDate.find(security_name);
					if (lastDealDateIt == ticker2lastDealDate.end()) {
						finishSecurityStats(security_name, date_from, dealDate);
						ticker2lastDealDate[security_name] = dealDate;
					} else {
						date lastDealDate = lastDealDateIt->second;
						finishSecurityStats(security_name, lastDealDate, dealDate);
						ticker2lastDealDate[security_name] = dealDate;
					}
							
					stats& stats = ticker2stats[security_name];
					stats.stocks += stocks;
					stats.profit += -stocks * price - commission;
					stats.lastPrice = price;
					stats.commision += commission;
					totalFortsCommision += commission;

					 /*std::cout << security_name << " stats stocks = " << stats.stocks << " stats.profit = "
					 << stats.profit << " stats.lastPrice = " << stats.lastPrice << " stats.commision = "  << stats.commision << std::endl;*/
				}
			}
		}
	}

	// не по всем инструментам в последний день были сделки
	// но статистику нам надо построить по всем инструментам по всем дням
	// так что завершаем построение статистики
	for (auto value : ticker2stats) {
		std::string security_name = value.first;
		auto lastDealDateIt = ticker2lastDealDate.find(security_name);
		if (lastDealDateIt == ticker2lastDealDate.end()) {
			finishSecurityStats(security_name, date_from, date_to + days(1));
			//ticker2lastDealDate[security_name] = date_to;
		} else {
			date lastDealDate = lastDealDateIt->second;
			finishSecurityStats(security_name, lastDealDate, date_to + days(1));
			//ticker2lastDealDate[security_name] = date_to;
		}
	}

	// печатаем суммарную статистику по дням
	day_iterator dayIt(date_from);
	int n = 0;
	while (dayIt <= date_to) {
		std::cout << to_simple_string(*dayIt) << " " << dayIt->day_of_week() << " ";
		double sumProfit = 0;
		double sumCom = 0;
		for (auto value : ticker2stats) {
			std::string security_name = value.first;
			std::vector<stats>& allStats = ticker2AllStats[security_name];
			stats& curStats = allStats[n];
			double profit = curStats.profit + curStats.lastPrice * curStats.stocks;
			/*if (dayIt == date_to) {
				std::cout << security_name << " profit = " << curStats.profit << 
					" lastPrice = " << curStats.lastPrice << " stocks = " << curStats.stocks << std::endl;
			}*/
			/*std::cout << std::fixed << std::setw(15) << security_name << " " << std::setw(15) <<
				profit << std::setw(15) << allStats[n].commision << std::endl;*/
			sumProfit += profit;
			sumCom += curStats.commision;
		}
		std::cout << std::fixed << std::setprecision(2) << " Profit: " << sumProfit 
			<< " Commission: " << sumCom << " n = " << n << std::endl;
		++dayIt; ++n;
	}

	for (int groupNumber = 0; groupNumber < nGroups; ++groupNumber) {
		day_iterator dayIt(date_from);
		int n = 0;
		while (dayIt <= date_to) {
			std::cout << to_simple_string(*dayIt);
			double sumProfit = 0;
			double sumCom = 0;
			for (int j = 0; j < groupSize; ++j) {
				std::string& security_name = groups[groupNumber][j];
				std::cout << std::fixed << std::setw(12) << security_name;
				auto allStatsIt = ticker2AllStats.find(security_name);
				if (allStatsIt == ticker2AllStats.end()) {
					continue;
				}
				std::vector<stats>& allStats = allStatsIt->second;
				stats& curStats = allStats[n];
				double profit = curStats.profit + curStats.lastPrice * curStats.stocks;
				sumProfit += profit;
				sumCom += curStats.commision;
			}
			std::cout << std::setprecision(2) << " Prft: " << sumProfit 
				<< " Comm.: " << sumCom << std::endl;
			++dayIt; ++n;
		}
	}

	//// combined profit by group
	for (int groupNumber = 0; groupNumber < nGroups; ++groupNumber) {
		double profit = 0;
		double commision = 0;
		std::cout <<  std::setw(2) << groupNumber << " ";
		for (int j = 0; j < groupSize; ++j) {
			std::string& ticker = groups[groupNumber][j];
			std::cout << std::fixed << std::setw(12) << ticker;

			auto got = ticker2stats.find(ticker);
			if (got == ticker2stats.end()) {
				//std::cout << std::endl << ticker << " skipped, not found." << std::endl << std::endl;
				std::cout << "$";
				continue;
			}
			stats& stats = got->second;
			if (stats.profit == 0) {
				std::cout << "stats.profit == 0! ticker = " << ticker << std::endl;
			}
			profit += stats.profit + stats.lastPrice * stats.stocks;
			commision += stats.commision;
		}
		std::cout << std::setw(12) << profit << std::setw(12) << commision << std::endl;
	}

	// печатаем суммарную статистику за последний день
	std::cout << std::endl << std::fixed << std::setw(20) << "Security name" << std::setw(20) <<
		"Profit > 0 good!" << std::setw(20) << "Commission" << std::endl << std::endl;

	double totalProfit = 0;
	for (auto value : ticker2stats) {
		double profit = value.second.profit + value.second.lastPrice * value.second.stocks;
		std::cout << std::fixed << std::setw(20) << value.first << " " << std::setw(20) <<
			profit << std::setw(20) << value.second.commision << std::endl;
		totalProfit += profit;
	}

	std::cout << std::endl << std::setprecision(2) << "Total profit: " << totalProfit 
		<< " Spot commision: " << totalSpotCommision  << " Forts commision: " 
		<< totalFortsCommision << std::endl << std::endl;

	return 0;
}


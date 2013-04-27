// this might not be needed on linux systems...
#define BOOST_THREAD_USE_LIB

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread/thread.hpp>
#include <boost/tokenizer.hpp>

// libs: boost_system, boost_filesystem, boost_iostream, boost_thread (maybe not on linux systems)
// on windwos, also link: wsock_32, ws2_32

bool running = true;

// quit upon ^c from the console
void handleSignal(int signal)
{
    running = false;
}

std::vector<std::string> get_vector_from_file(std::string filename)
{
    std::vector<std::string> objs;
    
    std::ifstream file(filename);
    
    if (!file.is_open())
    {
        std::cerr << "Error: Could not open file: " << filename << std::endl;
        
        return objs;
    }
    
    std::string line;
    while (std::getline(file, line))
    {
        if (line.size() > 0) objs.push_back(line);
    }
    
    file.close();
    
    return objs;
    
}

std::vector<std::pair<std::string, std::string> > get_pairs_from_file(std::string filename)
{
    std::vector<std::pair<std::string, std::string> > pairs;
    std::ifstream file(filename);
    
    if (!file.is_open())
    {
        std::cerr << "Error: could not open file: " << filename << std::endl;
        return pairs;
    }
    
    std::string line;
    while (std::getline(file, line))
    {
        size_t pos = line.find(" ");
        if (pos != std::string::npos)
        {
            std::string s1, s2;
            s1 = line.substr(0, pos);
            s2 = line.substr(pos+1, std::string::npos);
            pairs.push_back(std::make_pair(s1, s2));
        }
    }
    
    return pairs;
}


std::vector<std::pair<std::string, uint16_t> > get_counts_from_pairs(std::vector<std::pair<std::string, std::string> > pairs)
{
    std::vector<std::pair<std::string, uint16_t> > counts;
    for (std::vector<std::pair<std::string, std::string> >::iterator pairs_it = pairs.begin(); pairs_it != pairs.end(); pairs_it++)
    {
        try
        {
            counts.push_back(std::make_pair((*pairs_it).first, boost::lexical_cast<uint16_t>((*pairs_it).second)));
        }
        catch (boost::bad_lexical_cast &)
        {
            std::cerr << "bad lexical cast: " << (*pairs_it).first << ", " << (*pairs_it).second << std::endl;
        }
    }
    
    return counts;
    
}

void write_counts(std::vector<std::pair<std::string, uint16_t> > pairs, std::string filename)
{
    std::fstream file(filename, std::fstream::out | std::fstream::trunc);
    
    for (uint16_t i = 0; i < pairs.size(); i++)
    {
        file << pairs[i].first << " " << pairs[i].second << std::endl;
    }
    
    file.close();    
}


int32_t get_user_index_in_counts(std::string user, std::vector<std::pair<std::string, uint16_t> > counts)
{
    bool found = false;
    
    int32_t index;
    
    for (uint16_t i = 0; i < counts.size() && !found; i++)
    {
        if (boost::iequals(counts[i].first, user))
        {
            index = i;
            found = true;
        }
    }
    
    if (found)
    {
        return index;
    }
    else
    {
        return -1;
    }    
}


int main()
{
    std::string name = "BrickyBot";
    std::string source = ":BrickyBot!~BrickyBot@BrickHouse";
    std::string server = "irc.p2p-network.net";
    std::string port = "6667";
    std::string channel = "#MyLittleAlcoholic";
    bool auto_reconnect = false;
    
    // Load and parse the options file for configurability
    std::vector<std::pair<std::string, std::string> > options = get_pairs_from_file("data/options");
    for (std::vector<std::pair<std::string, std::string> >::iterator options_it = options.begin(); options_it != options.end(); options_it++)
    {
        if ((*options_it).first == "name") name = (*options_it).second;
        else if ((*options_it).first == "source") source = (*options_it).second;
        else if ((*options_it).first == "server") server = (*options_it).second;
        else if ((*options_it).first == "port") port = (*options_it).second;
        else if ((*options_it).first == "channel") channel = (*options_it).second;
        else if ((*options_it).first == "auto_reconnect" && (*options_it).second == "true") auto_reconnect = true;
    }

    // initialize vectors    
    std::vector<std::string> operators = get_vector_from_file("data/operators");
    std::vector<std::string> episodes = get_vector_from_file("data/pony_episodes");
    std::vector<std::pair<std::string, uint16_t> > drinks = get_counts_from_pairs(get_pairs_from_file("data/drinkcount"));
    std::vector<std::pair<std::string, std::string> > responses = get_pairs_from_file("data/responses");
    std::vector<std::pair<std::string, uint16_t> > hits = get_counts_from_pairs(get_pairs_from_file("data/hitcount"));
    
    // set up signal handlers
    signal(SIGABRT, handleSignal);
    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);
    
    try
    {
        // networking shit
        boost::asio::io_service io_service;
        boost::asio::ip::tcp::resolver resolver(io_service);
        boost::asio::ip::tcp::resolver::query query(server, port);
        boost::asio::ip::tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
        boost::asio::ip::tcp::socket socket(io_service);
        boost::asio::connect(socket, endpoint_iterator);

        boost::system::error_code ignored_error;
        uint64_t num_in = 0;
        bool first_ping = false;
        bool in_channel = false;

        
        while (running)
        {
            // Get a message from the server
            boost::array<char, 1024> buf;
            boost::system::error_code error;
            size_t len = socket.read_some(boost::asio::buffer(buf), error);

            // Check for an error or a closed connection
            if (error == boost::asio::error::eof)
            {
                // Connection closed cleanly by peer
                running = false;
            }
            else if (error)
            {
                // Some other dipshit error
                throw boost::system::system_error(error);
            }
            else
            {
                // No error. Parse the message.
                // First form a string from the char array
                std::string message_str(buf.begin(), buf.begin() + len);

                // Tokenize the string for multiple messages
                boost::char_separator<char> msg_sep("\r\n");
                boost::tokenizer<boost::char_separator<char> > msg_tok(message_str, msg_sep);

                // Place all the tokens into a vector
                std::vector<std::string> message_strings;
                for (boost::tokenizer<boost::char_separator<char> >::iterator it = msg_tok.begin(); it != msg_tok.end(); ++it)
                    message_strings.push_back(*it);

                // Process each message
                for (uint16_t i = 0; i < message_strings.size(); ++i)
                {
                    std::cout << "incoming: " << message_strings[i] << std::endl;

                    // Tokenize the message
                    boost::char_separator<char> sep(" ");
                    boost::tokenizer<boost::char_separator<char> > tok(message_strings[i], sep);
                    std::vector<std::string> tokens;
                    for (boost::tokenizer<boost::char_separator<char> >::iterator it = tok.begin(); it != tok.end(); ++it)
                        tokens.push_back(*it);

                    // Now process the message.
                    if (tokens[0] == "PING")
                    {
                        // Build a new string consisting of PONG followed by everything else
                        std::string send_str = "PONG";

                        for (uint16_t j = 1; j < tokens.size(); ++j)
                            send_str += " " + tokens[j];

                        // Add the carriage return and newline, as per IRC protocol
                        send_str += "\r\n";

                        // Print the string to the console
                        std::cout << "\toutgoing: " << send_str << std::endl;

                        // Send it to the server
                        boost::asio::write(socket, boost::asio::buffer(send_str), ignored_error);

                        first_ping = true;
                    }

                    num_in++;

                    
                    // Stupid IRC bullshit. Send some info after getting three messages.
                    if (num_in == 3)
                    {
                        // Send the USER/NICK message pair after 3 receives
                        // (What a whore; receieves three times before telling its name...)
                        std::string nick_msg_str = "NICK " + name + "\r\n";
                        std::string user_msg_str = "USER " + name + " 0 * :" + name + "\r\n";
                        boost::asio::write(socket, boost::asio::buffer(nick_msg_str), ignored_error);
                        boost::asio::write(socket, boost::asio::buffer(user_msg_str), ignored_error);

                        std::cout << "\toutgoing: " << nick_msg_str;
                        std::cout << "\toutgoing: " << user_msg_str;
                    }

                    // Join the channel when appropriate
                    if (first_ping && !in_channel)
                    {
                        // Connect after being pinged
                        std::string send_str = source + " JOIN :" + channel + "\r\n";
                        boost::asio::write(socket, boost::asio::buffer(send_str), ignored_error);

                        // wait a bit...
                        //boost::this_thread::sleep(boost::posix_time::milliseconds(1000));

                        in_channel = true;
                    }

                    // Bot is in the channel. The interesting shit is here
                    if (in_channel)
                    {
                        // Used to send messages
                        std::string send_str;

                        // Print the number of messages from the server
                        std::cout << num_in << std::endl;

                        if (tokens[1] == "PRIVMSG") // gettin a message
                        {
                            // Get the name of the sending user
                            boost::char_separator<char> user_tok_sep(":!");
                            boost::tokenizer<boost::char_separator<char> > user_tok(tokens[0], user_tok_sep);
                            std::string sending_user = *(user_tok.begin());
                            
                            // Check whether the sending user is an operator
                            bool user_is_operator = (std::find(operators.begin(), operators.end(), sending_user) != operators.end());
                            
                            
                            // Find the start of the text
                            uint16_t chat_start_index = 2;
                            while (tokens[chat_start_index][0] != ':') ++chat_start_index;
                            
                            // Add the first token (the start of the message) and remove the leading colon
                            std::string chat_text = tokens[chat_start_index];
                            chat_text.erase(0, 1);
                            
                            for (uint16_t chat_text_index = chat_start_index + 1; chat_text_index < tokens.size(); ++chat_text_index)
                            {
                                chat_text += " " + tokens[chat_text_index];
                            }

                            std::cout << "\t\t\tmessage: " << chat_text << std::endl;
                            
                            boost::char_separator<char> chat_sep(" ");
                            boost::tokenizer<boost::char_separator<char> > chat_tok(chat_text, chat_sep);
                            std::string chat_command = chat_text;
                            
                            int16_t chat_arg = -1;
                            
                            if (std::distance(chat_tok.begin(), chat_tok.end()) > 0)
                            {
                                chat_command =  *(chat_tok.begin());
                                
                                
                                boost::tokenizer<boost::char_separator<char> >::iterator chat_tok_it = chat_tok.begin();
                                if (++chat_tok_it != chat_tok.end())
                                {
                                    try
                                    {
                                        chat_arg = boost::lexical_cast<int16_t>(*chat_tok_it);
                                    }
                                    catch (boost::bad_lexical_cast &)
                                    {
                                        std::cerr << "bad lexical cast: " << *chat_tok_it << std::endl;
                                    }
                                }
                                
                                if (chat_arg < -1) chat_arg = -1;
                            }
                                
                            /*
                             *    This is where the bot actually does interesting things.
                             */
                            
                            // quit if an operator tells it to
                            if (boost::iequals(chat_text, name + ": quit") && user_is_operator)
                            {
                                send_str = source + " QUIT " + channel + " :quitting\r\n";
                                boost::asio::write(socket, boost::asio::buffer(send_str), ignored_error);
                                running = false;
                            }
                            
                            // reload operators file when ordered to by an operator
                            if (boost::iequals(chat_text, name + ": reload ops") && user_is_operator)
                            {
                                operators =  get_vector_from_file("data/operators");
                                send_str = source + " PRIVMSG " + channel + " :Reloaded operators.\r\n";
                                boost::asio::write(socket, boost::asio::buffer(send_str), ignored_error);
                            }

                            // .poni
                            if (boost::iequals(chat_command, ".poni"))
                            {
                                // pick a random episode
                                int episode = rand() % episodes.size();
                                send_str = source + " PRIVMSG " + channel + " :" + episodes[episode] + "\r\n";
                                boost::asio::write(socket, boost::asio::buffer(send_str), ignored_error);
                                
                            }
                            
                            // reload mlp episode list when ordered to by an operator
                            if (boost::iequals(chat_text, name + ": reload poni") && user_is_operator)
                            {
                                episodes =  get_vector_from_file("data/pony_episodes");
                                send_str = source + " PRIVMSG " + channel + " :Reloaded poni.\r\n";
                                boost::asio::write(socket, boost::asio::buffer(send_str), ignored_error);
                            }
                            
                            if (boost::iequals(chat_command, ".shot") || boost::iequals(chat_command, ".drink"))
                            {
                                // Find the user in the vector
                                int32_t user_index = get_user_index_in_counts(sending_user, drinks);
                                uint16_t current_drink_count;
                                
                                // If the user is already in the vector, increment the count.
                                // Otherwise, add the user and initialize them with one drink.
                                if (user_index != -1)
                                {
                                    drinks[user_index].second += chat_arg == -1? 1: chat_arg;
                                    current_drink_count = drinks[user_index].second;
                                }
                                else
                                {
                                    drinks.push_back(std::make_pair(sending_user, 1));
                                    current_drink_count = chat_arg == -1? 1 : chat_arg;
                                }
                                
                                // Write the changes to disk
                                write_counts(drinks, "data/drinkcount");
                                
                                // Convert the number of drinks into a string
                                std::ostringstream num_to_str;
                                num_to_str << current_drink_count;
                                std::string drink_count_str = num_to_str.str();
                                
                                // send the current number of drinks
                                send_str = source + " PRIVMSG " + channel + " :" + sending_user + ": you are now at " +
                                        drink_count_str + " drink(s).\r\n";
                                boost::asio::write(socket, boost::asio::buffer(send_str), ignored_error);
                            }
                            
                            if (boost::iequals(chat_command, ".weed") || boost::iequals(chat_command, ".hit") || boost::iequals(chat_command, ".toke"))
                            {
                                // Find the user in the vector
                                int32_t user_index = get_user_index_in_counts(sending_user, hits);
                                uint16_t current_hit_count;
                                
                                // If the user is already in the vector, increment the count.
                                // Otherwise, add the user and initialize them with one drink.
                                if (user_index != -1)
                                {
                                    hits[user_index].second += chat_arg == -1? 1: chat_arg;
                                    current_hit_count = hits[user_index].second;
                                }
                                else
                                {
                                    hits.push_back(std::make_pair(sending_user, 1));
                                    current_hit_count = chat_arg == -1? 1: chat_arg;
                                }
                                
                                // Write the changes to disk
                                 write_counts(hits, "data/hitcount");
                                
                                // Convert the number of drinks into a string
                                std::ostringstream num_to_str;
                                num_to_str << current_hit_count;
                                std::string hit_count_str = num_to_str.str();
                                
                                // send the current number of drinks
                                send_str = source + " PRIVMSG " + channel + " :" + sending_user + ": you are now at " +
                                        hit_count_str + " hit(s).\r\n";
                                boost::asio::write(socket, boost::asio::buffer(send_str), ignored_error);                                
                            }
                            
                            if (boost::iequals(chat_command, ".reset"))
                            {
                                ////////////////// Reset Drink Count
                                
                                // Find the user in the vector
                                int32_t user_index = get_user_index_in_counts(sending_user, drinks);
                                
                                // set that user's shot/drink count to zero
                                if (user_index != -1)
                                {
                                    drinks[user_index].second = 0;
                                }
                                else
                                {
                                    drinks.push_back(std::make_pair(sending_user, 0));
                                }
                                
                                // write changes to disk
                                write_counts(drinks, "data/drinkcount");
                                
                                ////////////////// Reset Hit Count
                                
                                // Find the user in the vector
                                user_index = get_user_index_in_counts(sending_user, hits);
                                
                                // set that user's hit count to zero
                                if (user_index != -1)
                                {
                                    hits[user_index].second = 0;
                                }
                                else
                                {
                                    hits.push_back(std::make_pair(sending_user, 0));
                                }
                                
                                // write changes to disk
                                write_counts(hits, "data/hitcount");
                                
                                // send acknowledgement
                                send_str = source + " PRIVMSG " + channel + " :" + sending_user + ": you are now at 0 drinks and 0 hits.\r\n";
                                boost::asio::write(socket, boost::asio::buffer(send_str), ignored_error);
                            }
                            
                            
                            
                            // Print user's counts
                            if (boost::iequals(chat_command, ".count"))
                            {
                                // Find the user in the vector
                                int32_t user_index = get_user_index_in_counts(sending_user, drinks);
                                
                                send_str = source + " PRIVMSG " + channel + " :" + sending_user + ": you are now at ";
                                
                                if (user_index != -1)
                                {
                                    std::ostringstream num_to_str;
                                    num_to_str << drinks[user_index].second;
                                    send_str += num_to_str.str();
                                }
                                else
                                {
                                    send_str += "0";
                                }

                                send_str += " drinks and ";
                                
                                user_index = get_user_index_in_counts(sending_user, hits);
                                
                                if (user_index != -1)
                                {
                                    std::ostringstream num_to_str;
                                    num_to_str << hits[user_index].second;
                                    send_str += num_to_str.str();
                                }
                                else
                                {
                                    send_str += "0";
                                }
                                
                                send_str += " hits.\r\n";

                                
                                boost::asio::write(socket, boost::asio::buffer(send_str), ignored_error);
                            }
                            
                            // Print the channel's total number of recorded drinks
                            if (boost::iequals(chat_command, ".chtotal"))
                            {
                                uint32_t total = 0;
                                for (std::vector<std::pair<std::string, uint16_t> >::iterator drinks_it = drinks.begin(); drinks_it != drinks.end(); drinks_it++)
                                {
                                    total += (*drinks_it).second;
                                }
                                
                                std::ostringstream num_to_str;
                                num_to_str << total;
                                send_str = source + " PRIVMSG " + channel + " :Total drinks: " + num_to_str.str() + "; ";
                                
                                total = 0;
                                for (std::vector<std::pair<std::string, uint16_t> >::iterator hits_it = hits.begin(); hits_it != hits.end(); hits_it++)
                                {
                                    total += (*hits_it).second;
                                }
                                
                                std::ostringstream num_to_str2;
                                num_to_str2 << total;
                                send_str += "Total hits: " + num_to_str2.str() + "\r\n";
                                
                                boost::asio::write(socket, boost::asio::buffer(send_str), ignored_error);
                            }
                            
                            // Send all the drink info
                            if (boost::iequals(chat_command, ".chdrinks"))
                            {
                                send_str = source + " PRIVMSG " + channel + " :Channel Drinks: ";
                                
                                for (uint16_t i = 0; i < drinks.size(); i++)
                                {
                                    if (drinks[i].second > 0)
                                    {
                                        std::ostringstream num_to_str;
                                        num_to_str << drinks[i].second;
                                        send_str += drinks[i].first + ": " + num_to_str.str() + ". ";
                                    }
                                }
                                
                                send_str += "\r\n";
                                
                                boost::asio::write(socket, boost::asio::buffer(send_str), ignored_error);
                            }

                            // Send all the hits info
                            if (boost::iequals(chat_command, ".chhits"))
                            {
                                send_str = source + " PRIVMSG " + channel + " :Channel Hits: ";
                                
                                for (uint16_t i = 0; i < hits.size(); i++)
                                {
                                    if (hits[i].second > 0)
                                    {
                                        std::ostringstream num_to_str;
                                        num_to_str << hits[i].second;
                                        send_str += hits[i].first + ": " + num_to_str.str() + ". ";
                                    }
                                }
                                
                                send_str += "\r\n";
                                
                                boost::asio::write(socket, boost::asio::buffer(send_str), ignored_error);
                            }
                            
                            // Operator command to reset all drinks
                            if (boost::iequals(chat_text, name + ": reset all drinks") && user_is_operator)
                            {
                                for (uint16_t i = 0; i < drinks.size(); i++)
                                {
                                    drinks[i].second = 0;
                                }
                                
                                write_counts(drinks, "data/drinkcount");
                                
                                send_str = source + " PRIVMSG " + channel + " :All drinks were reset.\r\n";
                                boost::asio::write(socket, boost::asio::buffer(send_str), ignored_error);
                            }
                            
                            // Operator command to reset all hits
                            if (boost::iequals(chat_text, name + ": reset all hits") && user_is_operator)
                            {
                                for (uint16_t i = 0; i < hits.size(); i++)
                                {
                                    hits[i].second = 0;
                                }
                                
                                write_counts(hits, "data/hitcount");
                                
                                send_str = source + " PRIVMSG " + channel + " :All hits were reset.\r\n";
                                boost::asio::write(socket, boost::asio::buffer(send_str), ignored_error);
                            }
                            
                            // Operator command to reset both counters
                            if (boost::iequals(chat_text, name + ": reset all") && user_is_operator)
                            {
                                for (uint16_t i = 0; i < drinks.size(); i++)
                                {
                                    drinks[i].second = 0;
                                }
                                
                                write_counts(drinks, "data/drinkcount");
                                
                                
                                for (uint16_t i = 0; i < hits.size(); i++)
                                {
                                    hits[i].second = 0;
                                }
                                
                                write_counts(hits, "data/hitcount");   
                                
                                send_str = source + " PRIVMSG " + channel + " :All drinks and hits were reset.\r\n";
                                boost::asio::write(socket, boost::asio::buffer(send_str), ignored_error);
                            }
                            
                            // modifying responses while running
                            if (boost::iequals(chat_text, name + ": reload responses") && user_is_operator)
                            {
                                responses = get_pairs_from_file("data/responses");
                                send_str = source + " PRIVMSG " + channel + " :Reloaded responses.\r\n";
                                boost::asio::write(socket, boost::asio::buffer(send_str), ignored_error);
                            }
                            
                            // simple response commands
                            for (std::vector<std::pair<std::string, std::string> >::iterator resp_it = responses.begin(); resp_it != responses.end(); resp_it++)
                            {
                                if (boost::iequals(chat_command, (*resp_it).first))
                                {
                                    send_str = source + " PRIVMSG " + channel + " :" + (*resp_it).second + "\r\n";
                                    boost::asio::write(socket, boost::asio::buffer(send_str), ignored_error);
                                }
                            }
                        }
                        else if (tokens[1] == "KICK" && tokens[3] == name) // The bot was kicked
                        {
                            boost::char_separator<char> user_tok_sep(":!");
                            boost::tokenizer<boost::char_separator<char> > user_tok(tokens[0], user_tok_sep);
                            std::string sending_user = *(user_tok.begin());
                            bool user_is_operator = (std::find(operators.begin(), operators.end(), sending_user) != operators.end());

                            if (auto_reconnect && !user_is_operator)
                            {
                                // rejoin the channel
                                std::string send_str = source + " JOIN :" + channel + "\r\n";
                                boost::asio::write(socket, boost::asio::buffer(send_str), ignored_error);
                            }
                            else
                            {
                                // don't rejoin; just quit
                                send_str = source + " QUIT " + channel + " :quitting\r\n";
                                boost::asio::write(socket, boost::asio::buffer(send_str), ignored_error);
                                running = false;
                            }
                        }
                    }
                }
            }
        }
    }
    catch (std::exception e)
    {
        std::cout << "Exception..." << std::endl;
        std::cerr << e.what() << std::endl;
    }

    return 0;
}

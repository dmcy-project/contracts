#include <eosiolib/eosio.hpp>
#include <eosiolib/print.hpp>
#include <eosiolib/transaction.hpp>

#define MANAGER_ACCOUNT "dmcymanagera"

using namespace eosio;

struct post
{
   name user;
   uint64_t size;
   std::string title;
   std::string uri;
   std::string type;
   std::string description;
};

extern "C"
{
   void apply(uint64_t receiver, uint64_t code, uint64_t action)
   {
      eosio_assert(
         code == receiver && action == name{"post"}.value,
         "Only post action is allowed");
      auto data = unpack_action_data<post>();
      require_auth(data.user);

      // Verify second action of current trx exists and is MANAGER_ACCOUNT::validatepost
      auto act = get_action(1, 1);
      eosio_assert(
         act.account==name{MANAGER_ACCOUNT},
         "account of second action must be MANAGER_ACCOUNT");
      eosio_assert(
         act.name==name{"validatepost"},
         "name of second action must be validatepost");         
      // title
      eosio_assert(
         data.title.length() < 200 && data.title.length() > 10,
         "title length must larger than 10 and less than 200");
      // uri
      eosio_assert(
         data.uri.length() < 2000 && data.uri.length() > 50,
         "uri length must larger than 50 and less than 2000");
      eosio_assert(
         data.uri.compare(0, 7, std::string{"magnet:"}) == 0,
         "uri must start with \"magnet:\"");
      // size   
      eosio_assert(
         data.size > 0,
         "size must larger than 0");
      // type   
      eosio_assert(
         data.type.length() < 50,
         "type length must less than 50");
      // description   
      eosio_assert(
         data.description.length() < 2000,
         "description length must less than 2000");
   }
}
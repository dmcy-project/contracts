#include <eosiolib/asset.hpp>
#include <eosiolib/eosio.hpp>
#include <eosiolib/print.hpp>
#include <eosiolib/transaction.hpp>

#define POSTS_ACCOUNT "dmcypostacct"
#define SYSTEM_TOKEN_SYMBOL "TLOS"
#define POST_MIN_DELAY 10*60*1000000ULL // 10 minutes
#define REFUND_DELAY 2*24*3600*1000000ULL // 48 hours
#define DEPOSIT_AMOUNT 50000 // 5.0000

using namespace eosio;

class [[eosio::contract]] manager : public contract
{

public:
   using contract::contract;

   [[eosio::action]] 
   void upsertwl(name user, std::string displayname) {
      require_auth( _self );
      whitelistTable whitelist( _self, _self.value );
      auto iterator = whitelist.find(user.value);
      if( iterator == whitelist.end() ){
         whitelist.emplace(_self, [&]( auto& row ) {
            row.user = user;
            row.displayname = displayname;
         });
      }
      else {
         whitelist.modify(iterator, _self, [&]( auto& row ) {
            row.displayname = displayname;
         });
      }
   }

   [[eosio::action]] 
   void erasewl(name user) {
      require_auth( _self );
      whitelistTable whitelist( _self, _self.value );
      auto iterator = whitelist.find(user.value);
      eosio_assert(iterator != whitelist.end(), "Record does not exist");
      whitelist.erase(iterator);
   }

   [[eosio::action]] 
   void upsertbl(name user, uint64_t time) {
      require_auth( _self );
      blacklistTable blacklist( _self, _self.value );
      auto iterator = blacklist.find(user.value);
      if( iterator == blacklist.end() ){
         blacklist.emplace(_self, [&]( auto& row ) {
            row.user = user;
            row.time = time;
         });
      }
      else {
         blacklist.modify(iterator, _self, [&]( auto& row ) {
            row.time = time;
         });
      }
   }

   [[eosio::action]] 
   void erasebl(name user) {
      require_auth( _self );
      blacklistTable blacklist( _self, _self.value );
      auto iterator = blacklist.find(user.value);
      eosio_assert(iterator != blacklist.end(), "Record does not exist");
      blacklist.erase(iterator);
   }  

   [[eosio::action]] 
   void seizedeposit(name user, uint64_t time, std::string reason) {
      require_auth( _self );
      depositsTable deposits( _self, user.value );
      auto iterator = deposits.find(time);
      eosio_assert(iterator != deposits.end(), "Record does not exist");
      deposits.erase(iterator);
   }

   [[eosio::action]] 
   void refund(name user, uint64_t deposittime) {
      require_auth( user );
      // Verify refund is after deposite time + REFUND_DELAY
      eosio_assert(
         current_time() > deposittime + REFUND_DELAY,
         "can only refund after 48 hours");      

      // Verify user is not in the blacklist
      blacklistTable blacklist( _self, _self.value );
      auto iteratorBL = blacklist.find(user.value);
      eosio_assert(iteratorBL == blacklist.end(), "user is blacklisted");

      // Verify deposit record exists
      depositsTable deposits( _self, user.value );
      auto iterator = deposits.find(deposittime);
      eosio_assert(iterator != deposits.end(), "deposit record does not exist");

      // transfer deposit
      action refund = action(
         permission_level{_self,"active"_n},
         "eosio.token"_n,
         "transfer"_n,
         std::make_tuple(_self, user, (*iterator).quantity, std::string(""))
      );
      refund.send();

      // erase deposit record
      deposits.erase(iterator);
   }
   [[eosio::action]] 
   void closeuser(name user) {
      require_auth( user );
      userinfoTable userinfoTab( _self, user.value );
      auto iterator = userinfoTab.find(user.value);
      eosio_assert(iterator != userinfoTab.end(), "user info does not exist");
      eosio_assert(
         current_time() > (*iterator).lastposttime + POST_MIN_DELAY,
         "can only close user info record 10 minutes after last post"); 
      userinfoTab.erase(iterator);            
   }

   [[eosio::action]] 
   void validatepost(name user) {
      require_auth(user);
      // Verify first action of current trx exists and is POSTS_ACCOUNT::post
      auto act = get_action(1, 0);
      eosio_assert(
         act.account==name{POSTS_ACCOUNT},
         "account of first action must be POSTS_ACCOUNT");
      eosio_assert(
         act.name==name{"post"},
         "name of first action must be post");  

      // If user is in the blacklist, fail
      blacklistTable blacklist( _self, _self.value );
      auto iteratorBL = blacklist.find(user.value);
      eosio_assert(
         iteratorBL == blacklist.end(),
         "user is in the blacklist");

      // If current time is less than POST_MIN_DELAY + last post time, fail
      userinfoTable userinfoTab( _self, user.value );
      auto iteratorUI = userinfoTab.find(user.value);
      if( iteratorUI == userinfoTab.end() ){
         // If userinfo not exist, create it with current time and allow post
         userinfoTab.emplace(user, [&]( auto& row ) {
            row.user = user;
            row.lastposttime = current_time();
            row.ext = "";
         });
      }else{
         // If current time is less than POST_MIN_DELAY + last post time, fail
         eosio_assert(
            current_time() > (*iteratorUI).lastposttime + POST_MIN_DELAY,
            "can only post 10 minutes after last post");
         // Current time is larger than POST_MIN_DELAY + last post time,
         // update last post time to current_time and allow post
         userinfoTab.modify(iteratorUI, user, [&]( auto& row ) {
            row.lastposttime = current_time();
         });
      }

      // If user is not in the whitelist, need to pay deposit
      whitelistTable whitelist( _self, _self.value );
      auto iteratorWL = whitelist.find(user.value);
      if( iteratorWL == whitelist.end() ){
         // The user isn't in the whitelist, check if user has paid deposit
         // Verify third action of current trx is payment
         auto act = get_action(1, 2);
         eosio_assert(
            act.account==name{"eosio.token"},
            "account of third action must be eosio.token");
         eosio_assert(
            act.name==name{"transfer"},
            "name of third action must be transfer");  
         asset postDeposit(DEPOSIT_AMOUNT, symbol(SYSTEM_TOKEN_SYMBOL, 4));   
         auto transfer = act.data_as<transferData>();
         print(transfer.quantity);
         print(postDeposit);
         eosio_assert(transfer.from == user, "deposit must from user");
         eosio_assert(transfer.to == _self, "recipient must be this contract");
         eosio_assert(transfer.quantity == postDeposit, "deposit mismatched");
         // Inser deposit info into the user's deposits table
         depositsTable deposits( _self, user.value );
         deposits.emplace(user, [&]( auto& row ) {
            row.time = current_time();
            row.quantity = postDeposit;
         });

         // Verify fourth action of current trx does not exists (security check)
         int actionResult = ::get_action( 1, 3, nullptr, 0 );
         eosio_assert(actionResult<0, "three actions allowed in trx with deposit"); 
      }else{
         //The user is in the whitelist
         // Verify third action of current trx does not exists (security check)
         int actionResult = ::get_action( 1, 2, nullptr, 0 );
         eosio_assert(actionResult<0, "two actions allowed in trx without deposit"); 
      }

      // post action from POSTS_ACCOUNT should go through by now
   }

private:
   struct transferData {
      name    from;
      name    to;
      asset   quantity;
      std::string  memo;
   };

   struct [[eosio::table]] whitelistusr {
      name user;
      std::string displayname;
      uint64_t primary_key() const { return user.value; }
   };
   using whitelistTable = eosio::multi_index<"whitelist"_n, whitelistusr>;

   struct [[eosio::table]] blacklistusr {
      name user;
      uint64_t time;
      uint64_t primary_key() const { return user.value; }
   };   
   using blacklistTable = eosio::multi_index<"blacklist"_n, blacklistusr>;

   struct [[eosio::table]] deposit {
      uint64_t time;
      asset    quantity;
      uint64_t primary_key()const { return time; }
   };     
   using depositsTable = eosio::multi_index<"deposits"_n, deposit>;

   struct [[eosio::table]] userinfo {
      name user;
      uint64_t lastposttime;
      std::string ext;
      uint64_t primary_key() const { return user.value; }
   };     
   using userinfoTable = eosio::multi_index<"userinfo"_n, userinfo>;   
};


extern "C" { 
   void apply( uint64_t receiver, uint64_t code, uint64_t action ) { 
      if( code == receiver ) { 
         switch( action ) { 
            EOSIO_DISPATCH_HELPER(manager, (upsertwl)(erasewl)(upsertbl)(erasebl)(seizedeposit)(refund)(closeuser)(validatepost)) 
         } 
      }else if( code == name{"eosio.token"}.value){

      } else{
         eosio_assert( 0, "code or action not allowed" );
      }
   } 
} 